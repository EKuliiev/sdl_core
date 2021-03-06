# Copyright (c) 2016, Ford Motor Company
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following
# disclaimer in the documentation and/or other materials provided with the
# distribution.
#
# Neither the name of the Ford Motor Company nor the names of its contributors
# may be used to endorse or promote products derived from this software
# without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

include(${CMAKE_SOURCE_DIR}/tools/cmake/helpers/sources.cmake)

include_directories(
  ${GMOCK_INCLUDE_DIRECTORY}
  ${COMPONENTS_DIR}/application_manager/include/
  ${COMPONENTS_DIR}/application_manager/include/application_manager/
  ${COMPONENTS_DIR}/application_manager/include/application_manager/commands/
  ${COMPONENTS_DIR}/application_manager/include/application_manager/commands/hmi/
  ${COMPONENTS_DIR}/application_manager/include/application_manager/commands/mobile/
  ${COMPONENTS_DIR}/application_manager/test/include/
  ${COMPONENTS_DIR}/application_manager/test/include/application_manager/
)

set(COMMANDS_TEST_DIR ${AM_TEST_DIR}/commands)

file(GLOB SOURCES
  ${COMMANDS_TEST_DIR}/*
  ${COMPONENTS_DIR}/application_manager/test/mock_message_helper.cc
  ${COMPONENTS_DIR}/application_manager/src/smart_object_keys.cc
  ${COMPONENTS_DIR}/application_manager/src/message.cc
  ${COMMANDS_TEST_DIR}/hmi/*
  ${COMMANDS_TEST_DIR}/hmi/hmi_notifications/*
  ${COMMANDS_TEST_DIR}/mobile/*
)

set(LIBRARIES
  gmock
  Utils
  SmartObjects
  jsoncpp
  HMI_API
  MOBILE_API
  ApplicationManager
  AMHMICommandsLibrary
  AMMobileCommandsLibrary
  connectionHandler
)

create_cotired_test("commands_test" "${SOURCES}" "${LIBRARIES}" )
