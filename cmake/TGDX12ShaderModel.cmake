# SPDX-License-Identifier: Apache-2.0 OR MIT
include_guard(GLOBAL)

# TG selects 6_7 before loading the engine. Other projects retain stock 6_2.
set(TG_DX12_SHADER_MODEL "6_2" CACHE STRING "DX12 shader target: 6_2 or 6_7")
set_property(CACHE TG_DX12_SHADER_MODEL PROPERTY STRINGS 6_2 6_7)
if(NOT TG_DX12_SHADER_MODEL MATCHES "^6_[27]$")
    message(FATAL_ERROR "TG_DX12_SHADER_MODEL must be 6_2 or 6_7")
endif()
message(STATUS "TG DX12 shader model: ${TG_DX12_SHADER_MODEL}")

function(tg_set_dx12_shader_model target)
    get_target_property(actual_target "${target}" ALIASED_TARGET)
    if(NOT actual_target)
        set(actual_target "${target}")
    endif()
    string(REPLACE "_" "" shader_model_number "${TG_DX12_SHADER_MODEL}")
    target_compile_definitions("${actual_target}" PRIVATE TG_DX12_SHADER_MODEL=${shader_model_number})
endfunction()
