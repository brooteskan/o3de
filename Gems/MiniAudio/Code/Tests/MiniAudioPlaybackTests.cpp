// SPDX-License-Identifier: Apache-2.0 OR MIT
#include "../Source/Clients/MiniAudioPlaybackComponent.h"
#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/UnitTest/MockComponentApplication.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzTest/AzTest.h>
#include <miniaudio.h>
#include <cmath>
#include <cstring>
#include <vector>

namespace MiniAudio::Tests
{
    class TestSound final : public SoundAsset
    {
    public:
        void Status(AssetStatus value) { m_status = value; }
    };
    class SoundHandler final : public AZ::Data::AssetHandler
    {
    public:
        AZ::Data::AssetPtr CreateAsset(const AZ::Data::AssetId&, const AZ::Data::AssetType&) override { return aznew TestSound; }
        void DestroyAsset(AZ::Data::AssetPtr asset) override { delete asset; }
        void GetHandledAssetTypes(AZStd::vector<AZ::Data::AssetType>& types) override { types.push_back(azrtti_typeid<SoundAsset>()); }
        LoadResult LoadAssetData(const AZ::Data::Asset<AZ::Data::AssetData>&,
            AZStd::shared_ptr<AZ::Data::AssetDataStream>, const AZ::Data::AssetFilterCB&) override { return LoadResult::Error; }
    };
    class HeadlessAudio final : public MiniAudioRequests
    {
    public:
        ma_engine engine{};
        ma_sound_group level{};
        float volume=1;
        ma_engine* GetSoundEngine() override { return &engine; }
        ma_sound* GetLevelSoundGroup() override { return &level; }
        void SetGlobalVolume(float gain) override { volume=gain;ma_engine_set_volume(&engine, gain); }
        float GetGlobalVolume() const override { return volume; }
        void SetGlobalVolumeInDecibels(float gain) override { SetGlobalVolume(ma_volume_db_to_linear(gain)); }
        AZ::u32 GetChannelCount() const override { return 2; }
    };
    class MiniAudioPlaybackTests : public UnitTest::LeakDetectionFixture
    {
    protected:
        void SetUp() override
        {
            UnitTest::LeakDetectionFixture::SetUp();
            AZ::Data::AssetManager::Create({});
            AZ::Data::AssetManager::Instance().RegisterHandler(&handler, azrtti_typeid<SoundAsset>());
            transformDescriptor.reset(AzFramework::TransformComponent::CreateDescriptor());
            playbackDescriptor.reset(MiniAudioPlaybackComponent::CreateDescriptor());
            auto config=ma_engine_config_init();config.noDevice=MA_TRUE;config.channels=2;config.sampleRate=48000;
            ASSERT_EQ(ma_engine_init(&config,&audio.engine),MA_SUCCESS);
            ASSERT_EQ(ma_sound_group_init(&audio.engine,0,nullptr,&audio.level),MA_SUCCESS);
            MiniAudioInterface::Register(&audio);
            ma_engine_listener_set_direction(&audio.engine,0,0,1,0);
            ma_engine_listener_set_world_up(&audio.engine,0,0,0,1);
        }
        void TearDown() override
        {
            MiniAudioInterface::Unregister(&audio);
            ma_sound_group_uninit(&audio.level);ma_engine_uninit(&audio.engine);
            AZ::Data::AssetManager::Instance().DispatchEvents();
            AZ::Data::AssetManager::Instance().UnregisterHandler(&handler);
            AZ::Data::AssetManager::Destroy();
            playbackDescriptor.reset();transformDescriptor.reset();
            UnitTest::LeakDetectionFixture::TearDown();
        }
        AZ::Data::Asset<SoundAsset> Clip(bool ready)
        {
            auto asset=AZ::Data::AssetManager::Instance().CreateAsset<SoundAsset>(AZ::Data::AssetId(AZ::Uuid::CreateRandom()));
            auto& wav=asset->m_data;wav.resize(44+48000*2);
            auto u16=[&](size_t at,AZ::u16 v){std::memcpy(wav.data()+at,&v,2);};
            auto u32=[&](size_t at,AZ::u32 v){std::memcpy(wav.data()+at,&v,4);};
            std::memcpy(wav.data(),"RIFF",4);u32(4,static_cast<AZ::u32>(wav.size()-8));
            std::memcpy(wav.data()+8,"WAVEfmt ",8);u32(16,16);u16(20,1);u16(22,1);
            u32(24,48000);u32(28,96000);u16(32,2);u16(34,16);std::memcpy(wav.data()+36,"data",4);u32(40,96000);
            for(size_t i=0;i<48000;++i)u16(44+i*2,static_cast<AZ::u16>(static_cast<AZ::s16>(std::sin(i*.05)*10000)));
            static_cast<TestSound*>(asset.Get())->Status(ready?AZ::Data::AssetData::AssetStatus::Ready:AZ::Data::AssetData::AssetStatus::Loading);
            return asset;
        }
        std::pair<double,double> Energy()
        {
            float discard[4096]{};ma_engine_read_pcm_frames(&audio.engine,discard,2048,nullptr);
            float samples[2048]{};ma_engine_read_pcm_frames(&audio.engine,samples,1024,nullptr);
            std::pair<double,double> result{};
            for(size_t i=0;i<1024;++i){result.first+=samples[i*2]*samples[i*2];result.second+=samples[i*2+1]*samples[i*2+1];}
            return result;
        }
        HeadlessAudio audio;
        SoundHandler handler;
        AZStd::unique_ptr<AZ::ComponentDescriptor> transformDescriptor,playbackDescriptor;
    };

    TEST_F(MiniAudioPlaybackTests, AutoFollowUpdatesRenderedSoundForEntityParentAndListenerMovementAndReportsEnd)
    {
        AZ::Entity parent("Platform"),source("Sound source");
        testing::NiceMock<UnitTest::MockComponentApplication> app;
        ON_CALL(app,AddEntity(testing::_)).WillByDefault(testing::Return(true));
        ON_CALL(app,RemoveEntity(testing::_)).WillByDefault(testing::Return(true));
        ON_CALL(app,FindEntity(testing::_)).WillByDefault([&](const AZ::EntityId& id)->AZ::Entity* {
            return id==parent.GetId()?&parent:(id==source.GetId()?&source:nullptr);
        });
        MiniAudioPlaybackComponentConfig config;config.m_sound=Clip(true);
        config.m_enableSpatialization=true;config.m_autoFollowEntity=true;
        config.m_minimumDistance=1;config.m_maximumDistance=1000000;
        parent.CreateComponent<AzFramework::TransformComponent>();source.CreateComponent<AzFramework::TransformComponent>();
        source.CreateComponent<MiniAudioPlaybackComponent>(config);
        parent.Init();parent.Activate();source.Init();source.Activate();
        AZ::TransformBus::Event(source.GetId(),&AZ::TransformInterface::SetParent,parent.GetId());
        AZ::TransformBus::Event(source.GetId(),&AZ::TransformInterface::SetLocalTranslation,AZ::Vector3(-2,2,0));
        auto playing=[&]{bool value=false;MiniAudioPlaybackRequestBus::EventResult(value,source.GetId(),&MiniAudioPlaybackRequests::IsPlaying);return value;};
        MiniAudioPlaybackRequestBus::Event(source.GetId(),&MiniAudioPlaybackRequests::Play);
        EXPECT_TRUE(playing());
        auto left=Energy();EXPECT_GT(left.first,left.second*2);
        AZ::TransformBus::Event(parent.GetId(),&AZ::TransformInterface::SetWorldTranslation,AZ::Vector3(4,0,0));
        auto right=Energy();EXPECT_GT(right.second,right.first*2);
        ma_engine_listener_set_direction(&audio.engine,0,0,-1,0);
        auto turned=Energy();EXPECT_GT(turned.first,turned.second*2);
        ma_engine_listener_set_direction(&audio.engine,0,0,1,0);
        AZ::TransformBus::Event(source.GetId(),&AZ::TransformInterface::SetWorldTranslation,AZ::Vector3(0,2,0));
        auto near=Energy();
        AZ::TransformBus::Event(source.GetId(),&AZ::TransformInterface::SetWorldTranslation,AZ::Vector3(0,20,0));
        auto far=Energy();EXPECT_LT(far.first+far.second,(near.first+near.second)*.1);
        ma_engine_listener_set_position(&audio.engine,0,0,18,0);
        auto approached=Energy();EXPECT_GT(approached.first+approached.second,(far.first+far.second)*10);
        float output[2048]{};
        for(int i=0;i<50&&playing();++i)ma_engine_read_pcm_frames(&audio.engine,output,1024,nullptr);
        EXPECT_FALSE(playing());
        MiniAudioPlaybackRequestBus::Event(source.GetId(),&MiniAudioPlaybackRequests::Stop);
        MiniAudioPlaybackRequestBus::Event(source.GetId(),&MiniAudioPlaybackRequests::Play);EXPECT_TRUE(playing());
        source.Deactivate();parent.Deactivate();
        EXPECT_FALSE(playing());
        const auto silent=Energy();EXPECT_DOUBLE_EQ(silent.first+silent.second,0);
    }

    TEST_F(MiniAudioPlaybackTests, DeferredPlayStartsOnReadyButCancellationReplacementAndLoadFailureCannotLeaveItPending)
    {
        AZ::Entity source("Sound source");
        MiniAudioPlaybackComponentConfig config;config.m_sound=Clip(false);
        source.CreateComponent<AzFramework::TransformComponent>();source.CreateComponent<MiniAudioPlaybackComponent>(config);
        source.Init();source.Activate();
        auto pending=[&]{bool value=false;MiniAudioPlaybackRequestBus::EventResult(value,source.GetId(),&MiniAudioPlaybackRequests::IsPlaybackPending);return value;};
        auto playing=[&]{bool value=false;MiniAudioPlaybackRequestBus::EventResult(value,source.GetId(),&MiniAudioPlaybackRequests::IsPlaying);return value;};
        auto ready=[&](AZ::Data::Asset<SoundAsset> asset){static_cast<TestSound*>(asset.Get())->Status(AZ::Data::AssetData::AssetStatus::Ready);AZ::Data::AssetBus::Event(asset.GetId(),&AZ::Data::AssetEvents::OnAssetReady,asset);};
        MiniAudioPlaybackRequestBus::Event(source.GetId(),&MiniAudioPlaybackRequests::Play);EXPECT_TRUE(pending());EXPECT_FALSE(playing());
        ready(config.m_sound);EXPECT_FALSE(pending());EXPECT_TRUE(playing());
        auto cancelled=Clip(false);
        MiniAudioPlaybackRequestBus::Event(source.GetId(),&MiniAudioPlaybackRequests::SetSoundAsset,cancelled);
        MiniAudioPlaybackRequestBus::Event(source.GetId(),&MiniAudioPlaybackRequests::Play);EXPECT_TRUE(pending());
        MiniAudioPlaybackRequestBus::Event(source.GetId(),&MiniAudioPlaybackRequests::Stop);
        ready(cancelled);EXPECT_FALSE(pending());EXPECT_FALSE(playing());
        auto replaced=Clip(false),current=Clip(false);
        MiniAudioPlaybackRequestBus::Event(source.GetId(),&MiniAudioPlaybackRequests::SetSoundAsset,replaced);
        MiniAudioPlaybackRequestBus::Event(source.GetId(),&MiniAudioPlaybackRequests::Play);
        MiniAudioPlaybackRequestBus::Event(source.GetId(),&MiniAudioPlaybackRequests::SetSoundAsset,current);
        MiniAudioPlaybackRequestBus::Event(source.GetId(),&MiniAudioPlaybackRequests::Play);
        ready(replaced);EXPECT_TRUE(pending());EXPECT_FALSE(playing());
        static_cast<TestSound*>(current.Get())->Status(AZ::Data::AssetData::AssetStatus::Error);
        AZ_TEST_START_TRACE_SUPPRESSION;
        AZ::Data::AssetBus::Event(current.GetId(),&AZ::Data::AssetEvents::OnAssetError,current);
        AZ_TEST_STOP_TRACE_SUPPRESSION_NO_COUNT;
        EXPECT_FALSE(pending());EXPECT_FALSE(playing());
        source.Deactivate();
    }
}
AZ_UNIT_TEST_HOOK(DEFAULT_UNIT_TEST_ENV);
