/*
 * Copyright (c) 2013, Ford Motor Company
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided with the
 * distribution.
 *
 * Neither the name of the Ford Motor Company nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "remote_control/policy_helper.h"
#include "remote_control/remote_control_plugin.h"
#include "remote_control/rc_app_extension.h"
#include "utils/logger.h"

CREATE_LOGGERPTR_GLOBAL(logger_, "RemoteControl")

namespace remote_control {

void PolicyHelper::OnRSDLFunctionalityAllowing(
    bool allowed, RemotePluginInterface& rc_module) {
  rc_module.service()->SetRemoteControl(allowed);
}

void PolicyHelper::ChangeDeviceRank(const uint32_t device_handle,
                                    const std::string& rank,
                                    RemotePluginInterface& rc_module) {
  if (rank == "DRIVER") {
    rc_module.service()->SetPrimaryDevice(device_handle);
    // MarkApplications(device_handle);
  } else if (rank == "PASSENGER") {
    if (rc_module.service()->PrimaryDevice() == device_handle) {
      rc_module.service()->ResetPrimaryDevice();
      // MarkApplications(0);
    }
  } else {
    LOG4CXX_WARN(logger_, "Unknown device rank");
  }
}

void PolicyHelper::SetIsAppOnPrimaryDevice(
    application_manager::ApplicationSharedPtr app,
    RemotePluginInterface& rc_module) {
  MarkAppOnPrimaryDevice(app, rc_module.service()->PrimaryDevice(), rc_module);
}

void PolicyHelper::MarkAppOnPrimaryDevice(
    application_manager::ApplicationSharedPtr app,
    const uint32_t device_handle,
    RemotePluginInterface& rc_module) {
  application_manager::AppExtensionUID module_id = rc_module.GetModuleID();
  RCAppExtensionPtr extension =
      application_manager::AppExtensionPtr::static_pointer_cast<RCAppExtension>(
          app->QueryInterface(module_id));
  DCHECK(extension);
  bool is_driver = (app->device() == device_handle);
  extension->set_is_on_driver_device(is_driver);
}

void PolicyHelper::MarkApplications(const uint32_t device_handle,
                                    RemotePluginInterface& rc_module) {
  application_manager::AppExtensionUID module_id = rc_module.GetModuleID();
  std::vector<application_manager::ApplicationSharedPtr> applications =
      rc_module.service()->GetApplications(module_id);

  for (size_t i = 0; i < applications.size(); ++i) {
    MarkAppOnPrimaryDevice(applications[i], device_handle, rc_module);
  }
}

}  // namespace remote_control