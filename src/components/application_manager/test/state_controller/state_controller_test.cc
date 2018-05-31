/*
 * Copyright (c) 2017, Ford Motor Company
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

#include <ostream>
#include <tuple>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "application_manager/hmi_state.h"
#include "application_manager/state_controller_impl.h"
#include "application_manager/usage_statistics.h"
#include "application_manager/application_manager_impl.h"
#include "application_manager/mock_application.h"
#include "connection_handler/mock_connection_handler_settings.h"
#include "connection_handler/connection_handler_impl.h"
#include "transport_manager/mock_transport_manager.h"
#include "utils/lock.h"
#include "utils/data_accessor.h"
#include "utils/make_shared.h"
#include "application_manager/message_helper.h"
#include "application_manager/event_engine/event.h"
#include "application_manager/smart_object_keys.h"
#include "application_manager/mock_message_helper.h"
#include "policy/mock_policy_settings.h"
#include "policy/usage_statistics/mock_statistics_manager.h"
#include "protocol_handler/mock_session_observer.h"
#include "connection_handler/mock_connection_handler.h"
#include "application_manager/policies/mock_policy_handler_interface.h"
#include "application_manager/mock_event_dispatcher.h"
#include "application_manager/resumption/resume_ctrl.h"
#include "application_manager/mock_application_manager.h"

namespace am = application_manager;
using am::HmiState;
using am::HmiStatePtr;
using am::UsageStatistics;
using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::ReturnPointee;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::InSequence;
using ::testing::Truly;
using ::testing::AtLeast;

namespace test {
namespace components {
namespace state_controller_test {

namespace constants {
const uint32_t kCorrID = 314u;
const uint32_t kHMIAppID = 2718u;
}

bool operator==(const am::HmiState& lhs, const am::HmiState& rhs) {
  return std::make_tuple(lhs.hmi_level(),
                         lhs.audio_streaming_state(),
                         lhs.video_streaming_state(),
                         lhs.system_context()) ==
         std::make_tuple(rhs.hmi_level(),
                         rhs.audio_streaming_state(),
                         rhs.video_streaming_state(),
                         rhs.system_context());
}

bool operator!=(const am::HmiState& lhs, const am::HmiState& rhs) {
  return !(lhs == rhs);
}

struct HmiStatesComparator {
  const am::HmiStatePtr lhs_;

  HmiStatesComparator(const am::HmiStatePtr& state) : lhs_(state) {}

  bool operator()(const am::HmiStatePtr& rhs) const {
    return *lhs_ == *rhs;
  }
};

struct HmiStatesIDComparator {
  am::HmiState::StateID state_id_;

  HmiStatesIDComparator(am::HmiState::StateID state_id) : state_id_(state_id) {}

  bool operator()(am::HmiStatePtr state_ptr) const {
    return state_ptr->state_id() == state_id_;
  }
};

#define MEDIA true
#define NOT_MEDIA false
#define PROJECTION true
#define NOT_PROJECTION false
#define VC true
#define NOT_VC false
#define NAVI true
#define NOT_NAVI false

enum ApplicationType {
  APP_TYPE_NON_MEDIA,
  APP_TYPE_NAVI,
  APP_TYPE_MEDIA,
  APP_TYPE_PROJECTION,
  APP_TYPE_MEDIA_PROJECTION,
  APP_TYPE_ATTENUATED
};

std::ostream& operator<<(std::ostream& os, const ApplicationType src) {
#define ENUM_CASE(val) \
  case val:            \
    os << #val;        \
    break;

  switch (src) {
    ENUM_CASE(APP_TYPE_NON_MEDIA)
    ENUM_CASE(APP_TYPE_NAVI)
    ENUM_CASE(APP_TYPE_MEDIA)
    ENUM_CASE(APP_TYPE_PROJECTION)
    ENUM_CASE(APP_TYPE_MEDIA_PROJECTION)
    ENUM_CASE(APP_TYPE_ATTENUATED)
    default:
      os << "UNRECOGNIZED(" << static_cast<int>(src) << ")";
      break;
  }
#undef ENUM_CASE

  return os;
}

class StateControllerImplTest : public ::testing::Test {
 public:
  StateControllerImplTest()
      : ::testing::Test()
      , usage_stat("0",
                   utils::SharedPtr<usage_statistics::StatisticsManager>(
                       new usage_statistics_test::MockStatisticsManager))
      , applications_(application_set_, applications_lock_)
      , message_helper_mock_(
            *application_manager::MockMessageHelper::message_helper_mock()) {
    Mock::VerifyAndClearExpectations(&message_helper_mock_);
  }

  ~StateControllerImplTest() {
    Mock::VerifyAndClearExpectations(&message_helper_mock_);
  }

  NiceMock<application_manager_test::MockApplicationManager> app_manager_mock_;
  NiceMock<policy_test::MockPolicyHandlerInterface> policy_interface_;
  NiceMock<connection_handler_test::MockConnectionHandler>
      mock_connection_handler_;
  NiceMock<protocol_handler_test::MockSessionObserver> mock_session_observer_;

  am::UsageStatistics usage_stat;
  NiceMock<event_engine_test::MockEventDispatcher> mock_event_dispatcher_;

  am::ApplicationSet application_set_;
  mutable sync_primitives::Lock applications_lock_;
  DataAccessor<am::ApplicationSet> applications_;
  utils::SharedPtr<am::StateControllerImpl> state_ctrl_;

  am::ApplicationSharedPtr simple_app_;
  NiceMock<application_manager_test::MockApplication>* simple_app_ptr_;
  uint32_t simple_app_id_ = 1721;

  am::ApplicationSharedPtr navi_app_;
  NiceMock<application_manager_test::MockApplication>* navi_app_ptr_;
  uint32_t navi_app_id_ = 1762;

  am::ApplicationSharedPtr projection_app_;
  NiceMock<application_manager_test::MockApplication>* projection_app_ptr_;
  uint32_t projection_app_id_ = 1763;

  am::ApplicationSharedPtr media_app_;
  NiceMock<application_manager_test::MockApplication>* media_app_ptr_;
  uint32_t media_app_id_ = 1801;

  am::ApplicationSharedPtr vc_app_;
  NiceMock<application_manager_test::MockApplication>* vc_app_ptr_;
  uint32_t vc_app_id_ = 1825;

  am::ApplicationSharedPtr media_navi_app_;
  NiceMock<application_manager_test::MockApplication>* media_navi_app_ptr_;
  uint32_t media_navi_app_id_ = 1855;

  am::ApplicationSharedPtr media_projection_app_;
  NiceMock<application_manager_test::MockApplication>*
      media_projection_app_ptr_;
  uint32_t media_projection_app_id_ = 1856;

  am::ApplicationSharedPtr media_vc_app_;
  NiceMock<application_manager_test::MockApplication>* media_vc_app_ptr_;
  uint32_t media_vc_app_id_ = 1881;

  am::ApplicationSharedPtr navi_vc_app_;
  NiceMock<application_manager_test::MockApplication>* navi_vc_app_ptr_;
  uint32_t navi_vc_app_id_ = 1894;

  am::ApplicationSharedPtr media_navi_vc_app_;
  NiceMock<application_manager_test::MockApplication>* media_navi_vc_app_ptr_;
  uint32_t media_navi_vc_app_id_ = 1922;

  std::vector<am::HmiStatePtr> valid_states_for_audio_app_;
  std::vector<am::HmiStatePtr> valid_states_for_not_audio_app_;
  std::vector<am::HmiStatePtr> common_invalid_states_;
  std::vector<am::HmiStatePtr> invalid_states_for_not_audio_app_;
  std::vector<am::HmiStatePtr> invalid_states_for_audio_app_;
  std::vector<am::HmiState::StateID> valid_state_ids_;
  std::vector<am::HmiState::StateID> navi_valid_state_ids_;
  std::vector<am::ApplicationSharedPtr> applications_list_;

  connection_handler_test::MockConnectionHandlerSettings
      mock_connection_handler_settings;
  transport_manager_test::MockTransportManager mock_transport_manager;
  connection_handler::ConnectionHandlerImpl* conn_handler;
  application_manager::MockMessageHelper& message_helper_mock_;

  am::HmiStatePtr CreateHmiState(
      mobile_apis::HMILevel::eType hmi_level,
      mobile_apis::AudioStreamingState::eType audio_ss,
      mobile_apis::VideoStreamingState::eType video_ss,
      mobile_apis::SystemContext::eType system_context) {
    am::HmiStatePtr state =
        utils::MakeShared<am::HmiState>(simple_app_, app_manager_mock_);
    state->set_hmi_level(hmi_level);
    state->set_audio_streaming_state(audio_ss);
    state->set_video_streaming_state(video_ss);
    state->set_system_context(system_context);
    return state;
  }

  /**
   * @brief Template created for the future if different hmi
   * states are needed.
   */
  template <class HmiStateType>
  am::HmiStatePtr CreateHmiStateByHmiStateType(
      const mobile_apis::HMILevel::eType hmi_level,
      const mobile_apis::AudioStreamingState::eType audio_ss,
      const mobile_apis::VideoStreamingState::eType video_ss,
      const mobile_apis::SystemContext::eType system_context,
      const am::ApplicationSharedPtr app) {
    am::HmiStatePtr new_state =
        utils::MakeShared<HmiStateType>(app, app_manager_mock_);

    new_state->set_hmi_level(hmi_level);
    new_state->set_audio_streaming_state(audio_ss);
    new_state->set_video_streaming_state(video_ss);
    new_state->set_system_context(system_context);

    return new_state;
  }

  /**
   * @brief Prepare list of resultant HMI states for testing HMIState
   * @param result_hmi state will contain resultant HMI states.
   */
  void PrepareCommonStateResults(
      std::vector<am::HmiStatePtr>& result_hmi_state) {
    namespace HMILevel = mobile_apis::HMILevel;
    namespace AudioStreamingState = mobile_apis::AudioStreamingState;
    namespace VideoStreamingState = mobile_apis::VideoStreamingState;
    namespace SystemContext = mobile_apis::SystemContext;
    result_hmi_state.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    result_hmi_state.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_VRSESSION));
    result_hmi_state.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MENU));
    result_hmi_state.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_HMI_OBSCURED));
    result_hmi_state.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_ALERT));
    result_hmi_state.push_back(
        CreateHmiState(HMILevel::HMI_BACKGROUND,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
  }

  /**
   * @brief Prepare list of resultant HMI states for testing HMIState, for
   * case when SDL supports attenuated mode
   * @param result_hmi state will contain resultant HMI states.
   */
  void PrepareStateResultsForAttenuated(
      std::vector<am::HmiStatePtr>& result_hmi_state) {
    namespace HMILevel = mobile_apis::HMILevel;
    namespace AudioStreamingState = mobile_apis::AudioStreamingState;
    namespace VideoStreamingState = mobile_apis::VideoStreamingState;
    namespace SystemContext = mobile_apis::SystemContext;
    PrepareCommonStateResults(result_hmi_state);
    result_hmi_state.push_back(
        CreateHmiState(HMILevel::HMI_LIMITED,
                       AudioStreamingState::ATTENUATED,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    result_hmi_state.push_back(
        CreateHmiState(HMILevel::HMI_LIMITED,
                       AudioStreamingState::ATTENUATED,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    result_hmi_state.push_back(
        CreateHmiState(HMILevel::HMI_FULL,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    result_hmi_state.push_back(
        CreateHmiState(HMILevel::HMI_FULL,
                       AudioStreamingState::ATTENUATED,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
  }

  /**
   * @brief Prepare list of resultant HMI states for testing HMIState, for
   * case if phone call mode is active
   * @param result_hmi state will contain resultant HMI states.
   */
  void PreparePhoneCallHMIStateResults(
      std::vector<am::HmiStatePtr>& result_hmi_state, ApplicationType app_t) {
    namespace HMILevel = mobile_apis::HMILevel;
    namespace AudioStreamingState = mobile_apis::AudioStreamingState;
    namespace VideoStreamingState = mobile_apis::VideoStreamingState;
    namespace SystemContext = mobile_apis::SystemContext;

    switch (app_t) {
      case APP_TYPE_NON_MEDIA: {
        PrepareCommonStateResults(result_hmi_state);
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_FULL,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        break;
      }
      case APP_TYPE_MEDIA: {
        PrepareCommonStateResults(result_hmi_state);
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_BACKGROUND,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_BACKGROUND,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_BACKGROUND,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_BACKGROUND,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        break;
      }
      case APP_TYPE_NAVI: {
        PrepareCommonStateResults(result_hmi_state);
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_LIMITED,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_LIMITED,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_LIMITED,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_LIMITED,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        break;
      }
      default: { break; }
    }
  }

  /**
   * @brief Prepare list of resultant HMI states for testing HMIState, for
   * case if VR or TTS mode is active
   * @param result_hmi state will contain resultant HMI states.
   */
  void PrepareVRTTSHMIStateResults(
      std::vector<am::HmiStatePtr>& result_hmi_state, ApplicationType app_t) {
    namespace HMILevel = mobile_apis::HMILevel;
    namespace AudioStreamingState = mobile_apis::AudioStreamingState;
    namespace VideoStreamingState = mobile_apis::VideoStreamingState;
    namespace SystemContext = mobile_apis::SystemContext;
    switch (app_t) {
      case APP_TYPE_NON_MEDIA: {
        PrepareCommonStateResults(result_hmi_state);
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_FULL,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        break;
      }
      case APP_TYPE_MEDIA:
      case APP_TYPE_NAVI: {
        PrepareCommonStateResults(result_hmi_state);
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_LIMITED,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_LIMITED,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_FULL,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_FULL,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        break;
      }
      case APP_TYPE_ATTENUATED: {
        PrepareStateResultsForAttenuated(result_hmi_state);
        break;
      }
      default: { break; }
    }
  }

  /**
   * @brief Prepare list of resultant HMI states for testing HMIState, for
   * case if video streaming mode is active
   * @param result_hmi state will contain resultant HMI states.
   */
  void PrepareVideoStreamingHmiStateResults(
      std::vector<am::HmiStatePtr>& result_hmi_state, ApplicationType app_t) {
    namespace HMILevel = mobile_apis::HMILevel;
    namespace AudioStreamingState = mobile_apis::AudioStreamingState;
    namespace VideoStreamingState = mobile_apis::VideoStreamingState;
    namespace SystemContext = mobile_apis::SystemContext;
    switch (app_t) {
      case APP_TYPE_NON_MEDIA: {
        PrepareCommonStateResults(result_hmi_state);
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_FULL,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        break;
      }
      case APP_TYPE_MEDIA: {
        PrepareCommonStateResults(result_hmi_state);
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_LIMITED,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_LIMITED,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_FULL,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_FULL,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        break;
      }
      case APP_TYPE_NAVI: {
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_NONE,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_NONE,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_VRSESSION));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_NONE,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MENU));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_NONE,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_HMI_OBSCURED));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_NONE,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_ALERT));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_BACKGROUND,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_LIMITED,
                           AudioStreamingState::AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_LIMITED,
                           AudioStreamingState::ATTENUATED,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_FULL,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_FULL,
                           AudioStreamingState::AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        break;
      }
      default: { break; }
    }
  }

  /**
   * @brief Prepare list of resultant HMI states for testing HMIState, for
   * case if navi streaming mode and TTS mode are active and SDL supports
   * attenuated mode
   * @param result_hmi state will contain resultant HMI states.
   */
  void PrepareNaviStreamTTSStateResult(
      std::vector<am::HmiStatePtr>& result_hmi_state, ApplicationType app_t) {
    namespace HMILevel = mobile_apis::HMILevel;
    namespace AudioStreamingState = mobile_apis::AudioStreamingState;
    namespace VideoStreamingState = mobile_apis::VideoStreamingState;
    namespace SystemContext = mobile_apis::SystemContext;
    switch (app_t) {
      case APP_TYPE_NON_MEDIA: {
        PrepareCommonStateResults(result_hmi_state);
        result_hmi_state.push_back(
            CreateHmiState(HMILevel::HMI_FULL,
                           AudioStreamingState::NOT_AUDIBLE,
                           VideoStreamingState::NOT_STREAMABLE,
                           SystemContext::SYSCTXT_MAIN));
        break;
      }
      case APP_TYPE_MEDIA:
      case APP_TYPE_NAVI: {
        PrepareStateResultsForAttenuated(result_hmi_state);
        break;
      }
      default: { break; }
    }
  }

  ApplicationType AppType(uint32_t app_id) {
    // TODO(AOleynik): Currently there is ongoing discussion regarding mixed
    // application properties, i.e. is_media_application flag from RAI and
    // AppHmiTypes (NAVIGATION, etc.). Most likely logic should be changed
    // after conclusion on APPLINK-20231
    std::vector<am::ApplicationSharedPtr>::iterator app = std::find_if(
        applications_list_.begin(),
        applications_list_.end(),
        [app_id](am::ApplicationSharedPtr a) { return app_id == a->app_id(); });

    if (app == applications_list_.end()) {
      return APP_TYPE_NON_MEDIA;
    }

    if ((*app)->is_navi()) {
      return APP_TYPE_NAVI;
    }
    if ((*app)->is_media_application()) {
      return APP_TYPE_MEDIA;
    }
    return APP_TYPE_NON_MEDIA;
  }

  void TestSetState(am::ApplicationSharedPtr app,
                    am::HmiStatePtr hmi_state,
                    ApplicationType app_t,
                    void (StateControllerImplTest::*call_back)(
                        std::vector<am::HmiStatePtr>&, ApplicationType)) {
    InsertApplication(app);
    std::vector<am::HmiStatePtr> result_hmi_state;
    (this->*call_back)(result_hmi_state, app_t);
    std::vector<am::HmiStatePtr>::iterator it_begin;
    std::vector<am::HmiStatePtr>::iterator it_end;
    if (APP_TYPE_NON_MEDIA == app_t) {
      it_begin = valid_states_for_not_audio_app_.begin();
      it_end = valid_states_for_not_audio_app_.end();
      ASSERT_EQ(valid_states_for_not_audio_app_.size(),
                result_hmi_state.size());
    } else {
      it_begin = valid_states_for_audio_app_.begin();
      it_end = valid_states_for_audio_app_.end();
      ASSERT_EQ(valid_states_for_audio_app_.size(), result_hmi_state.size());
    }
    std::vector<am::HmiStatePtr>::iterator it_result_begin =
        result_hmi_state.begin();
    for (; it_begin != it_end; ++it_begin, ++it_result_begin) {
      hmi_state->set_parent(*it_begin);
      HmiStatesComparator st_comp(hmi_state);
      EXPECT_TRUE(st_comp(*it_result_begin))
          << "Wrong state for app type '" << app_t << "': Actual " << *hmi_state
          << ", expected " << **it_result_begin;
    }
  }

  void TestSetSeveralState(
      am::ApplicationSharedPtr app,
      am::HmiStatePtr first_hmi_state,
      am::HmiStatePtr second_hmi_state,
      ApplicationType app_t,
      void (StateControllerImplTest::*call_back)(std::vector<am::HmiStatePtr>&,
                                                 ApplicationType)) {
    InsertApplication(app);
    std::vector<am::HmiStatePtr> result_hmi_state;
    (this->*call_back)(result_hmi_state, app_t);
    std::vector<am::HmiStatePtr>::iterator it_begin;
    std::vector<am::HmiStatePtr>::iterator it_end;
    if (APP_TYPE_NON_MEDIA == app_t) {
      it_begin = valid_states_for_not_audio_app_.begin();
      it_end = valid_states_for_not_audio_app_.end();
      ASSERT_TRUE(result_hmi_state.size() ==
                  valid_states_for_not_audio_app_.size());
    } else {
      it_begin = valid_states_for_audio_app_.begin();
      it_end = valid_states_for_audio_app_.end();
      ASSERT_TRUE(result_hmi_state.size() ==
                  valid_states_for_audio_app_.size());
    }
    std::vector<am::HmiStatePtr>::iterator it_result_begin =
        result_hmi_state.begin();
    for (; it_begin != it_end; ++it_begin, ++it_result_begin) {
      first_hmi_state->set_parent(*it_begin);
      second_hmi_state->set_parent(first_hmi_state);
      HmiStatesComparator st_comp(second_hmi_state);
      ASSERT_TRUE(st_comp(*it_result_begin)) << *second_hmi_state
                                             << " != " << **it_result_begin;
    }
  }

  template <typename T, typename Q>
  void TestMixState(void (StateControllerImplTest::*call_back_result)(
      std::vector<am::HmiStatePtr>&, ApplicationType)) {
    for (am::ApplicationSharedPtr app : applications_list_) {
      const uint32_t app_id = app->app_id();
      const ApplicationType app_type = AppType(app_id);
      am::HmiStatePtr state_first =
          utils::MakeShared<T>(app, app_manager_mock_);
      am::HmiStatePtr state_second =
          utils::MakeShared<Q>(app, app_manager_mock_);
      TestSetSeveralState(
          app, state_first, state_second, app_type, call_back_result);
      TestSetSeveralState(
          app, state_second, state_first, app_type, call_back_result);
    }
  }

 protected:
  am::ApplicationSharedPtr ConfigureApp(
      NiceMock<application_manager_test::MockApplication>** app_mock,
      uint32_t app_id,
      bool media,
      bool navi,
      bool projection,
      bool vc) {
    *app_mock = new NiceMock<application_manager_test::MockApplication>;

    Mock::AllowLeak(*app_mock);  // WorkAround for googletest bug
    am::ApplicationSharedPtr app(*app_mock);

    ON_CALL(**app_mock, app_id()).WillByDefault(Return(app_id));
    ON_CALL(**app_mock, is_media_application()).WillByDefault(Return(media));
    ON_CALL(**app_mock, is_navi()).WillByDefault(Return(navi));
    ON_CALL(**app_mock, mobile_projection_enabled())
        .WillByDefault(Return(projection));
    ON_CALL(**app_mock, is_voice_communication_supported())
        .WillByDefault(Return(vc));
    ON_CALL(**app_mock, IsAudioApplication())
        .WillByDefault(Return(media || navi || vc));
    ON_CALL(**app_mock, IsVideoApplication())
        .WillByDefault(Return(navi || projection));

    EXPECT_CALL(**app_mock, usage_report())
        .WillRepeatedly(ReturnRef(usage_stat));

    return app;
  }

  void FillStatesLists() {
    namespace HMILevel = mobile_apis::HMILevel;
    namespace AudioStreamingState = mobile_apis::AudioStreamingState;
    namespace VideoStreamingState = mobile_apis::VideoStreamingState;
    namespace SystemContext = mobile_apis::SystemContext;
    // Valid states for not audio app
    valid_states_for_not_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    valid_states_for_not_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_VRSESSION));
    valid_states_for_not_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MENU));
    valid_states_for_not_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_HMI_OBSCURED));
    valid_states_for_not_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_ALERT));
    valid_states_for_not_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_BACKGROUND,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    valid_states_for_not_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_FULL,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));

    // Valid states audio app
    valid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    valid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_VRSESSION));
    valid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MENU));
    valid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_HMI_OBSCURED));
    valid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_ALERT));
    valid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_BACKGROUND,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    valid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_LIMITED,
                       AudioStreamingState::AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    valid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_LIMITED,
                       AudioStreamingState::ATTENUATED,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    valid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_FULL,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    valid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_FULL,
                       AudioStreamingState::AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));

    // Common Invalid States
    common_invalid_states_.push_back(
        CreateHmiState(HMILevel::INVALID_ENUM,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    common_invalid_states_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::INVALID_ENUM,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    common_invalid_states_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::INVALID_ENUM));
    common_invalid_states_.push_back(
        CreateHmiState(HMILevel::INVALID_ENUM,
                       AudioStreamingState::INVALID_ENUM,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    common_invalid_states_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::INVALID_ENUM,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::INVALID_ENUM));
    common_invalid_states_.push_back(
        CreateHmiState(HMILevel::INVALID_ENUM,
                       AudioStreamingState::INVALID_ENUM,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::INVALID_ENUM));

    // Invalid States for audio apps
    invalid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_LIMITED,
                       AudioStreamingState::NOT_AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    invalid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_BACKGROUND,
                       AudioStreamingState::AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    invalid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_BACKGROUND,
                       AudioStreamingState::ATTENUATED,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    invalid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    invalid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::ATTENUATED,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    invalid_states_for_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_NONE,
                       AudioStreamingState::ATTENUATED,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    // Invalid States for not audio apps
    invalid_states_for_not_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_LIMITED,
                       AudioStreamingState::ATTENUATED,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    invalid_states_for_not_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_LIMITED,
                       AudioStreamingState::AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    invalid_states_for_not_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_FULL,
                       AudioStreamingState::ATTENUATED,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));
    invalid_states_for_not_audio_app_.push_back(
        CreateHmiState(HMILevel::HMI_FULL,
                       AudioStreamingState::AUDIBLE,
                       VideoStreamingState::NOT_STREAMABLE,
                       SystemContext::SYSCTXT_MAIN));

    using StateID = am::HmiState::StateID;

    // Valid state ids for not navi app
    valid_state_ids_.push_back(StateID::STATE_ID_VR_SESSION);
    valid_state_ids_.push_back(StateID::STATE_ID_TTS_SESSION);
    valid_state_ids_.push_back(StateID::STATE_ID_PHONE_CALL);
    valid_state_ids_.push_back(StateID::STATE_ID_SAFETY_MODE);
    valid_state_ids_.push_back(StateID::STATE_ID_VIDEO_STREAMING);

    // Valid state ids for navi app
    navi_valid_state_ids_.push_back(StateID::STATE_ID_VR_SESSION);
    navi_valid_state_ids_.push_back(StateID::STATE_ID_TTS_SESSION);
    navi_valid_state_ids_.push_back(StateID::STATE_ID_PHONE_CALL);
    navi_valid_state_ids_.push_back(StateID::STATE_ID_SAFETY_MODE);
    navi_valid_state_ids_.push_back(StateID::STATE_ID_NAVI_STREAMING);
  }

  void ConfigureApps() {
    simple_app_ = ConfigureApp(&simple_app_ptr_,
                               simple_app_id_,
                               NOT_MEDIA,
                               NOT_NAVI,
                               NOT_PROJECTION,
                               NOT_VC);
    media_app_ = ConfigureApp(&media_app_ptr_,
                              media_app_id_,
                              MEDIA,
                              NOT_NAVI,
                              NOT_PROJECTION,
                              NOT_VC);
    navi_app_ = ConfigureApp(
        &navi_app_ptr_, navi_app_id_, NOT_MEDIA, NAVI, NOT_PROJECTION, NOT_VC);
    projection_app_ = ConfigureApp(&projection_app_ptr_,
                                   projection_app_id_,
                                   NOT_MEDIA,
                                   NOT_NAVI,
                                   PROJECTION,
                                   NOT_VC);
    vc_app_ = ConfigureApp(
        &vc_app_ptr_, vc_app_id_, NOT_MEDIA, NOT_NAVI, NOT_PROJECTION, VC);
    media_navi_app_ = ConfigureApp(&media_navi_app_ptr_,
                                   media_navi_app_id_,
                                   MEDIA,
                                   NAVI,
                                   NOT_PROJECTION,
                                   NOT_VC);
    media_projection_app_ = ConfigureApp(&media_projection_app_ptr_,
                                         media_projection_app_id_,
                                         MEDIA,
                                         NOT_NAVI,
                                         PROJECTION,
                                         NOT_VC);
    media_vc_app_ = ConfigureApp(&media_vc_app_ptr_,
                                 media_vc_app_id_,
                                 MEDIA,
                                 NOT_NAVI,
                                 NOT_PROJECTION,
                                 VC);
    navi_vc_app_ = ConfigureApp(&navi_vc_app_ptr_,
                                navi_vc_app_id_,
                                NOT_MEDIA,
                                NAVI,
                                NOT_PROJECTION,
                                VC);
    media_navi_vc_app_ = ConfigureApp(&media_navi_vc_app_ptr_,
                                      media_navi_vc_app_id_,
                                      MEDIA,
                                      NAVI,
                                      NOT_PROJECTION,
                                      VC);
    applications_list_.push_back(simple_app_);
    applications_list_.push_back(media_app_);
    applications_list_.push_back(navi_app_);
    applications_list_.push_back(projection_app_);
    applications_list_.push_back(vc_app_);
    applications_list_.push_back(media_navi_app_);
    applications_list_.push_back(media_projection_app_);
    applications_list_.push_back(media_vc_app_);
    applications_list_.push_back(navi_vc_app_);
    applications_list_.push_back(media_navi_vc_app_);
  }
  void CheckAppConfiguration() {
    ASSERT_EQ(simple_app_.get(), simple_app_ptr_);
    ASSERT_EQ(media_app_.get(), media_app_ptr_);
    ASSERT_EQ(navi_app_.get(), navi_app_ptr_);
    ASSERT_EQ(projection_app_.get(), projection_app_ptr_);
    ASSERT_EQ(vc_app_.get(), vc_app_ptr_);
    ASSERT_EQ(media_navi_app_.get(), media_navi_app_ptr_);
    ASSERT_EQ(media_projection_app_.get(), media_projection_app_ptr_);
    ASSERT_EQ(media_vc_app_.get(), media_vc_app_ptr_);
    ASSERT_EQ(navi_vc_app_.get(), navi_vc_app_ptr_);
    ASSERT_EQ(media_navi_vc_app_.get(), media_navi_vc_app_ptr_);

    ASSERT_EQ(simple_app_->app_id(), simple_app_id_);
    ASSERT_EQ(media_app_->app_id(), media_app_id_);
    ASSERT_EQ(navi_app_->app_id(), navi_app_id_);
    ASSERT_EQ(projection_app_->app_id(), projection_app_id_);
    ASSERT_EQ(vc_app_->app_id(), vc_app_id_);
    ASSERT_EQ(media_navi_app_->app_id(), media_navi_app_id_);
    ASSERT_EQ(media_projection_app_->app_id(), media_projection_app_id_);
    ASSERT_EQ(media_vc_app_->app_id(), media_vc_app_id_);
    ASSERT_EQ(navi_vc_app_->app_id(), navi_vc_app_id_);
    ASSERT_EQ(media_navi_vc_app_->app_id(), media_navi_vc_app_id_);

    ASSERT_FALSE(simple_app_->IsAudioApplication());
    ASSERT_TRUE(media_app_->IsAudioApplication());
    ASSERT_TRUE(navi_app_->IsAudioApplication());
    ASSERT_FALSE(projection_app_->IsAudioApplication());
    ASSERT_TRUE(vc_app_->IsAudioApplication());
    ASSERT_TRUE(media_navi_app_->IsAudioApplication());
    ASSERT_TRUE(media_projection_app_->IsAudioApplication());
    ASSERT_TRUE(media_vc_app_->IsAudioApplication());
    ASSERT_TRUE(navi_vc_app_->IsAudioApplication());
    ASSERT_TRUE(media_navi_vc_app_->IsAudioApplication());

    ASSERT_FALSE(simple_app_->IsVideoApplication());
    ASSERT_FALSE(media_app_->IsVideoApplication());
    ASSERT_TRUE(navi_app_->IsVideoApplication());
    ASSERT_TRUE(projection_app_->IsVideoApplication());
    ASSERT_FALSE(vc_app_->IsVideoApplication());
    ASSERT_TRUE(media_navi_app_->IsVideoApplication());
    ASSERT_TRUE(media_projection_app_->IsVideoApplication());
    ASSERT_FALSE(media_vc_app_->IsVideoApplication());
    ASSERT_TRUE(navi_vc_app_->IsVideoApplication());
    ASSERT_TRUE(media_navi_vc_app_->IsVideoApplication());

    ASSERT_FALSE(simple_app_->is_media_application());
    ASSERT_TRUE(media_app_->is_media_application());
    ASSERT_FALSE(navi_app_->is_media_application());
    ASSERT_FALSE(projection_app_->is_media_application());
    ASSERT_FALSE(vc_app_->is_media_application());
    ASSERT_TRUE(media_navi_app_->is_media_application());
    ASSERT_TRUE(media_projection_app_->is_media_application());
    ASSERT_TRUE(media_vc_app_->is_media_application());
    ASSERT_FALSE(navi_vc_app_->is_media_application());
    ASSERT_TRUE(media_navi_vc_app_->is_media_application());

    ASSERT_FALSE(simple_app_->is_navi());
    ASSERT_FALSE(media_app_->is_navi());
    ASSERT_TRUE(navi_app_->is_navi());
    ASSERT_FALSE(projection_app_->is_navi());
    ASSERT_FALSE(vc_app_->is_navi());
    ASSERT_TRUE(media_navi_app_->is_navi());
    ASSERT_FALSE(media_projection_app_->is_navi());
    ASSERT_FALSE(media_vc_app_->is_navi());
    ASSERT_TRUE(navi_vc_app_->is_navi());
    ASSERT_TRUE(media_navi_vc_app_->is_navi());

    ASSERT_FALSE(simple_app_->is_voice_communication_supported());
    ASSERT_FALSE(media_app_->is_voice_communication_supported());
    ASSERT_FALSE(navi_app_->is_voice_communication_supported());
    ASSERT_FALSE(projection_app_->is_voice_communication_supported());
    ASSERT_TRUE(vc_app_->is_voice_communication_supported());
    ASSERT_FALSE(media_navi_app_->is_voice_communication_supported());
    ASSERT_FALSE(media_projection_app_->is_voice_communication_supported());
    ASSERT_TRUE(media_vc_app_->is_voice_communication_supported());
    ASSERT_TRUE(navi_vc_app_->is_voice_communication_supported());
    ASSERT_TRUE(media_navi_vc_app_->is_voice_communication_supported());
  }

  void SetUp() OVERRIDE {
    ON_CALL(app_manager_mock_, event_dispatcher())
        .WillByDefault(ReturnRef(mock_event_dispatcher_));
    state_ctrl_ = utils::MakeShared<am::StateControllerImpl>(app_manager_mock_);

    ON_CALL(app_manager_mock_, applications())
        .WillByDefault(Return(applications_));
    ConfigureApps();
    CheckAppConfiguration();
    FillStatesLists();
    SetConnection();
  }

  void TearDown() OVERRIDE {
    delete conn_handler;
  }

  void SetConnection() {
    conn_handler = new connection_handler::ConnectionHandlerImpl(
        mock_connection_handler_settings, mock_transport_manager);
    ON_CALL(app_manager_mock_, connection_handler())
        .WillByDefault(ReturnRef(*conn_handler));
  }

  void SetBCActivateAppRequestToHMI(
      const hmi_apis::Common_HMILevel::eType hmi_lvl, uint32_t corr_id) {
    ON_CALL(mock_connection_handler_, get_session_observer())
        .WillByDefault(ReturnRef(mock_session_observer_));
    ON_CALL(app_manager_mock_, connection_handler())
        .WillByDefault(ReturnRef(mock_connection_handler_));
    ON_CALL(app_manager_mock_, GetPolicyHandler())
        .WillByDefault(ReturnRef(policy_interface_));
    smart_objects::SmartObjectSPtr bc_activate_app_request =
        new smart_objects::SmartObject();
    (*bc_activate_app_request)[am::strings::params]
                              [am::strings::correlation_id] = corr_id;
    ON_CALL(message_helper_mock_,
            GetBCActivateAppRequestToHMI(_, _, _, hmi_lvl, _, _))
        .WillByDefault(Return(bc_activate_app_request));

    ON_CALL(app_manager_mock_, ManageHMICommand(bc_activate_app_request))
        .WillByDefault(Return(true));
  }

  void ExpectSuccesfullSetHmiState(
      am::ApplicationSharedPtr app,
      NiceMock<application_manager_test::MockApplication>* app_mock,
      am::HmiStatePtr old_state,
      am::HmiStatePtr new_state) {
    EXPECT_CALL(*app_mock, CurrentHmiState())
        .WillOnce(Return(old_state))
        .WillOnce(Return(new_state));
    EXPECT_CALL(*app_mock,
                SetRegularState(Truly(HmiStatesComparator(new_state))));
    if (!HmiStatesComparator(old_state)(new_state)) {
      EXPECT_CALL(app_manager_mock_, SendHMIStatusNotification(app));
      EXPECT_CALL(
          app_manager_mock_,
          OnHMILevelChanged(
              app->app_id(), old_state->hmi_level(), new_state->hmi_level()));
    }
  }

  void ExpectAppChangeHmiStateDueToConflictResolving(
      am::ApplicationSharedPtr app,
      NiceMock<application_manager_test::MockApplication>* app_mock,
      am::HmiStatePtr old_state,
      am::HmiStatePtr new_state) {
    EXPECT_CALL(*app_mock, RegularHmiState())
        .WillOnce(Return(old_state))
        .WillOnce(Return(old_state));
    ExpectSuccesfullSetHmiState(app, app_mock, old_state, new_state);
  }

  void ExpectAppWontChangeHmiStateDueToConflictResolving(
      am::ApplicationSharedPtr app,
      NiceMock<application_manager_test::MockApplication>* app_mock,
      am::HmiStatePtr state) {
    EXPECT_CALL(*app_mock, RegularHmiState()).WillOnce(Return(state));
    EXPECT_CALL(app_manager_mock_, SendHMIStatusNotification(app)).Times(0);
    EXPECT_CALL(app_manager_mock_, OnHMILevelChanged(app->app_id(), _, _))
        .Times(0);
  }

  void InsertApplication(am::ApplicationSharedPtr app) {
    application_set_.insert(app);
    ON_CALL(app_manager_mock_, application(app->app_id()))
        .WillByDefault(Return(app));
  }

  am::HmiStatePtr UninitializedState() {
    return CreateHmiState(mobile_apis::HMILevel::INVALID_ENUM,
                          mobile_apis::AudioStreamingState::INVALID_ENUM,
                          mobile_apis::VideoStreamingState::INVALID_ENUM,
                          mobile_apis::SystemContext::INVALID_ENUM);
  }

  am::HmiStatePtr NoneState() {
    return CreateHmiState(mobile_apis::HMILevel::HMI_NONE,
                          mobile_apis::AudioStreamingState::NOT_AUDIBLE,
                          mobile_apis::VideoStreamingState::NOT_STREAMABLE,
                          mobile_apis::SystemContext::SYSCTXT_MAIN);
  }

  am::HmiStatePtr FullState() {
    return CreateHmiState(mobile_apis::HMILevel::HMI_FULL,
                          mobile_apis::AudioStreamingState::NOT_AUDIBLE,
                          mobile_apis::VideoStreamingState::NOT_STREAMABLE,
                          mobile_apis::SystemContext::SYSCTXT_MAIN);
  }

  am::HmiStatePtr FullAudibleState() {
    return CreateHmiState(mobile_apis::HMILevel::HMI_FULL,
                          mobile_apis::AudioStreamingState::AUDIBLE,
                          mobile_apis::VideoStreamingState::NOT_STREAMABLE,
                          mobile_apis::SystemContext::SYSCTXT_MAIN);
  }

  am::HmiStatePtr FullAudibleStreamableState() {
    return CreateHmiState(mobile_apis::HMILevel::HMI_FULL,
                          mobile_apis::AudioStreamingState::AUDIBLE,
                          mobile_apis::VideoStreamingState::STREAMABLE,
                          mobile_apis::SystemContext::SYSCTXT_MAIN);
  }

  am::HmiStatePtr LimitedAudibleState() {
    return CreateHmiState(mobile_apis::HMILevel::HMI_LIMITED,
                          mobile_apis::AudioStreamingState::AUDIBLE,
                          mobile_apis::VideoStreamingState::NOT_STREAMABLE,
                          mobile_apis::SystemContext::SYSCTXT_MAIN);
  }

  am::HmiStatePtr LimitedAudibleStreamableState() {
    return CreateHmiState(mobile_apis::HMILevel::HMI_LIMITED,
                          mobile_apis::AudioStreamingState::AUDIBLE,
                          mobile_apis::VideoStreamingState::STREAMABLE,
                          mobile_apis::SystemContext::SYSCTXT_MAIN);
  }

  am::HmiStatePtr BackgroundState() {
    return CreateHmiState(mobile_apis::HMILevel::HMI_BACKGROUND,
                          mobile_apis::AudioStreamingState::NOT_AUDIBLE,
                          mobile_apis::VideoStreamingState::NOT_STREAMABLE,
                          mobile_apis::SystemContext::SYSCTXT_MAIN);
  }

  void ApplyTempStatesForApplication(
      am::ApplicationSharedPtr app,
      NiceMock<application_manager_test::MockApplication>& app_mock,
      std::vector<am::HmiState::StateID>& state_ids) {
    using smart_objects::SmartObject;
    using am::event_engine::Event;
    namespace FunctionID = hmi_apis::FunctionID;
    using StateID = am::HmiState::StateID;

    EXPECT_CALL(app_mock, CurrentHmiState())
        .WillRepeatedly(Return(NoneState()));

    for (const StateID state_id : state_ids) {
      EXPECT_CALL(app_mock, AddHMIState(Truly(HmiStatesIDComparator(state_id))))
          .Times(1);
      switch (state_id) {
        case StateID::STATE_ID_VR_SESSION: {
          Event vr_start_event(FunctionID::VR_Started);
          state_ctrl_->on_event(vr_start_event);
          break;
        }
        case StateID::STATE_ID_TTS_SESSION: {
          Event tts_start_event(FunctionID::TTS_Started);
          state_ctrl_->on_event(tts_start_event);
          break;
        }
        case StateID::STATE_ID_PHONE_CALL: {
          Event phone_call_event(FunctionID::BasicCommunication_OnEventChanged);
          SmartObject message;
          message[am::strings::msg_params][am::hmi_notification::is_active] =
              true;
          message[am::strings::msg_params][am::hmi_notification::event_name] =
              hmi_apis::Common_EventTypes::PHONE_CALL;
          phone_call_event.set_smart_object(message);
          state_ctrl_->on_event(phone_call_event);
          break;
        }
        case StateID::STATE_ID_SAFETY_MODE: {
          Event emergency_event(FunctionID::BasicCommunication_OnEventChanged);
          SmartObject message;
          message[am::strings::msg_params][am::hmi_notification::is_active] =
              true;
          message[am::strings::msg_params][am::hmi_notification::event_name] =
              hmi_apis::Common_EventTypes::EMERGENCY_EVENT;
          emergency_event.set_smart_object(message);
          state_ctrl_->on_event(emergency_event);
          break;
        }
        case StateID::STATE_ID_VIDEO_STREAMING:
        case StateID::STATE_ID_NAVI_STREAMING: {
          state_ctrl_->OnVideoStreamingStarted(app);
          break;
        }
        default:
          break;
      }

      EXPECT_CALL(app_mock, AddHMIState(_)).Times(0);
    }
  }

  void RemoveTempStatesForApplication(
      am::ApplicationSharedPtr app,
      NiceMock<application_manager_test::MockApplication>& app_mock,
      std::vector<am::HmiState::StateID>& state_ids) {
    using smart_objects::SmartObject;
    using am::event_engine::Event;
    namespace FunctionID = hmi_apis::FunctionID;
    using StateID = am::HmiState::StateID;

    EXPECT_CALL(app_mock, CurrentHmiState())
        .WillRepeatedly(Return(NoneState()));

    for (const StateID state_id : state_ids) {
      EXPECT_CALL(app_mock, RemoveHMIState(state_id)).Times(1);

      EXPECT_CALL(app_mock, PostponedHmiState()).WillOnce(Return(NoneState()));
      EXPECT_CALL(app_mock, RemovePostponedState());

      switch (state_id) {
        case StateID::STATE_ID_VR_SESSION: {
          Event vr_stop_event(FunctionID::VR_Stopped);
          state_ctrl_->on_event(vr_stop_event);
          break;
        }
        case StateID::STATE_ID_TTS_SESSION: {
          Event tts_stop_event(FunctionID::TTS_Stopped);
          state_ctrl_->on_event(tts_stop_event);
          break;
        }
        case StateID::STATE_ID_PHONE_CALL: {
          Event phone_call_event(FunctionID::BasicCommunication_OnEventChanged);
          SmartObject message;
          message[am::strings::msg_params][am::hmi_notification::is_active] =
              false;
          message[am::strings::msg_params][am::hmi_notification::event_name] =
              hmi_apis::Common_EventTypes::PHONE_CALL;
          phone_call_event.set_smart_object(message);
          state_ctrl_->on_event(phone_call_event);
          break;
        }
        case StateID::STATE_ID_SAFETY_MODE: {
          Event emergency_event(FunctionID::BasicCommunication_OnEventChanged);
          SmartObject message;
          message[am::strings::msg_params][am::hmi_notification::is_active] =
              false;
          message[am::strings::msg_params][am::hmi_notification::event_name] =
              hmi_apis::Common_EventTypes::EMERGENCY_EVENT;
          emergency_event.set_smart_object(message);
          state_ctrl_->on_event(emergency_event);
          break;
        }
        case StateID::STATE_ID_VIDEO_STREAMING:
        case StateID::STATE_ID_NAVI_STREAMING: {
          state_ctrl_->OnVideoStreamingStopped(app);
          break;
        }
        default:
          break;
      }

      EXPECT_CALL(app_mock, RemoveHMIState(_)).Times(0);
    }
  }

  void CheckStateApplyingForApplication(
      am::ApplicationSharedPtr app,
      NiceMock<application_manager_test::MockApplication>& app_mock,
      std::vector<am::HmiState::StateID>& state_ids) {
    ApplyTempStatesForApplication(app, app_mock, state_ids);
    RemoveTempStatesForApplication(app, app_mock, state_ids);
  }
};

TEST_F(StateControllerImplTest, OnStateChangedWithEqualStates) {
  EXPECT_CALL(app_manager_mock_, SendHMIStatusNotification(_)).Times(0);
  EXPECT_CALL(app_manager_mock_, OnHMILevelChanged(_, _, _)).Times(0);
  EXPECT_CALL(*simple_app_ptr_, ResetDataInNone()).Times(0);

  for (auto state : valid_states_for_not_audio_app_) {
    state_ctrl_->OnStateChanged(simple_app_, state, state);
  }
}

TEST_F(StateControllerImplTest, OnStateChangedWithDifferentStates) {
  for (uint32_t i = 0; i < valid_states_for_not_audio_app_.size(); ++i) {
    for (uint32_t j = 0; j < valid_states_for_not_audio_app_.size(); ++j) {
      HmiStatesComparator comp(valid_states_for_not_audio_app_[i]);
      if (!comp(valid_states_for_not_audio_app_[j])) {
        EXPECT_CALL(app_manager_mock_, SendHMIStatusNotification(simple_app_))
            .Times(1);
        EXPECT_CALL(
            app_manager_mock_,
            OnHMILevelChanged(simple_app_id_,
                              valid_states_for_not_audio_app_[i]->hmi_level(),
                              valid_states_for_not_audio_app_[j]->hmi_level()))
            .Times(1);
        if (mobile_apis::HMILevel::HMI_NONE ==
            valid_states_for_not_audio_app_[j]->hmi_level()) {
          EXPECT_CALL(*simple_app_ptr_, ResetDataInNone()).Times(1);
        }
        state_ctrl_->OnStateChanged(simple_app_,
                                    valid_states_for_not_audio_app_[i],
                                    valid_states_for_not_audio_app_[j]);

        EXPECT_CALL(app_manager_mock_, SendHMIStatusNotification(_)).Times(0);
        EXPECT_CALL(app_manager_mock_, OnHMILevelChanged(_, _, _)).Times(0);
        EXPECT_CALL(*simple_app_ptr_, ResetDataInNone()).Times(0);
      }
    }
  }
}

TEST_F(StateControllerImplTest, OnStateChangedToNone) {
  HmiStatePtr none_state = NoneState();
  HmiStatePtr full_state = FullState();

  EXPECT_CALL(*simple_app_ptr_, ResetDataInNone()).Times(0);
  state_ctrl_->OnStateChanged(simple_app_, none_state, full_state);

  EXPECT_CALL(*simple_app_ptr_, ResetDataInNone()).Times(1);
  state_ctrl_->OnStateChanged(simple_app_, full_state, none_state);
}

TEST_F(StateControllerImplTest, MoveSimpleAppToValidStates) {
  HmiStatePtr initial_state = UninitializedState();

  for (auto state_to_setup : valid_states_for_not_audio_app_) {
    EXPECT_CALL(*simple_app_ptr_, CurrentHmiState())
        .WillOnce(Return(initial_state))
        .WillOnce(Return(state_to_setup));
    EXPECT_CALL(app_manager_mock_, SendHMIStatusNotification(simple_app_));
    EXPECT_CALL(app_manager_mock_,
                OnHMILevelChanged(simple_app_id_,
                                  initial_state->hmi_level(),
                                  state_to_setup->hmi_level()));

    EXPECT_CALL(*simple_app_ptr_,
                SetRegularState(Truly(HmiStatesComparator(state_to_setup))));
    state_ctrl_->SetRegularState(
        simple_app_, state_to_setup, /* send_activate_app = */ false);
    initial_state = state_to_setup;
  }
}

TEST_F(StateControllerImplTest, MoveAudioNotResumeAppToValidStates) {
  am::ApplicationSharedPtr audio_app = media_navi_vc_app_;
  NiceMock<application_manager_test::MockApplication>* audio_app_mock =
      media_navi_vc_app_ptr_;

  HmiStatePtr initial_state = UninitializedState();

  for (std::vector<HmiStatePtr>::iterator it =
           valid_states_for_audio_app_.begin();
       it != valid_states_for_audio_app_.end();
       ++it) {
    HmiStatePtr state_to_setup = *it;
    EXPECT_CALL(*audio_app_mock, CurrentHmiState())
        .WillOnce(Return(initial_state))
        .WillOnce(Return(state_to_setup));
    EXPECT_CALL(app_manager_mock_, SendHMIStatusNotification(audio_app));
    EXPECT_CALL(app_manager_mock_,
                OnHMILevelChanged(audio_app->app_id(),
                                  initial_state->hmi_level(),
                                  state_to_setup->hmi_level()));

    EXPECT_CALL(*audio_app_mock,
                SetRegularState(Truly(HmiStatesComparator(state_to_setup))));
    state_ctrl_->SetRegularState(
        media_navi_vc_app_, state_to_setup, /* send_activate_app = */ false);
    initial_state = state_to_setup;
  }
}

TEST_F(StateControllerImplTest, DISABLED_MoveAudioResumeAppToValidStates) {
  namespace HMILevel = mobile_apis::HMILevel;
  namespace AudioStreamingState = mobile_apis::AudioStreamingState;
  namespace VideoStreamingState = mobile_apis::VideoStreamingState;

  am::ApplicationSharedPtr audio_app = media_navi_vc_app_;
  NiceMock<application_manager_test::MockApplication>* audio_app_mock =
      media_navi_vc_app_ptr_;

  HmiStatePtr initial_state = UninitializedState();

  // Set all valid states for audio app
  for (HmiStatePtr state_to_setup : valid_states_for_audio_app_) {
    HmiStatePtr state_to_check = state_to_setup;
    // First time current state is initial, then it changes to setup state
    EXPECT_CALL(*audio_app_mock, CurrentHmiState())
        .WillOnce(Return(initial_state))
        .WillOnce(Return(state_to_setup));
    // Audio resume app when HMI level is LIMITED or FULL gets audible state
    if (state_to_setup->hmi_level() == HMILevel::HMI_LIMITED) {
      EXPECT_CALL(*audio_app_mock, is_resuming()).WillRepeatedly(Return(true));
      EXPECT_CALL(message_helper_mock_,
                  SendOnResumeAudioSourceToHMI(media_navi_vc_app_id_, _));
      state_to_check->set_audio_streaming_state(AudioStreamingState::AUDIBLE);

    } else {
      if (state_to_setup->hmi_level() == HMILevel::HMI_FULL) {
        state_to_check->set_audio_streaming_state(AudioStreamingState::AUDIBLE);
      }
      EXPECT_CALL(*audio_app_mock, is_resuming()).WillRepeatedly(Return(true));
    }
    EXPECT_CALL(app_manager_mock_, active_application())
        .WillRepeatedly(Return(am::ApplicationSharedPtr()));
    EXPECT_CALL(app_manager_mock_, SendHMIStatusNotification(audio_app))
        .Times(AtLeast(0));
    EXPECT_CALL(app_manager_mock_,
                OnHMILevelChanged(audio_app->app_id(),
                                  initial_state->hmi_level(),
                                  state_to_setup->hmi_level()))
        .Times(AtLeast(0));

    // Check that we set correct state
    EXPECT_CALL(*audio_app_mock,
                SetRegularState(Truly(HmiStatesComparator(state_to_check))));
    state_ctrl_->SetRegularState(
        media_navi_vc_app_, state_to_setup, /* send_activate_app = */ false);
    initial_state = state_to_setup;
  }
}

TEST_F(StateControllerImplTest, MoveAppFromValidStateToInvalid) {
  using am::HmiState;
  using am::HmiStatePtr;
  for (HmiStatePtr invalid_state : common_invalid_states_) {
    EXPECT_CALL(*simple_app_ptr_, CurrentHmiState()).Times(0);
    EXPECT_CALL(*simple_app_ptr_, is_resuming()).Times(0);
    EXPECT_CALL(app_manager_mock_, OnHMILevelChanged(_, _, _)).Times(0);
    EXPECT_CALL(*simple_app_ptr_, SetRegularState(_)).Times(0);
    state_ctrl_->SetRegularState(simple_app_, invalid_state, false);
  }

  for (HmiStatePtr invalid_state : common_invalid_states_) {
    EXPECT_CALL(*media_navi_vc_app_ptr_, CurrentHmiState()).Times(0);
    EXPECT_CALL(*media_navi_vc_app_ptr_, is_resuming()).Times(0);
    EXPECT_CALL(app_manager_mock_, OnHMILevelChanged(_, _, _)).Times(0);
    EXPECT_CALL(*media_navi_vc_app_ptr_, SetRegularState(_)).Times(0);
    state_ctrl_->SetRegularState(
        media_navi_vc_app_, invalid_state, /* send_activate_app = */ false);
  }
}

TEST_F(StateControllerImplTest,
       SetFullToSimpleAppWhileAnotherSimpleAppIsInFull) {
  am::ApplicationSharedPtr app_in_full;
  NiceMock<application_manager_test::MockApplication>* app_in_full_mock;

  am::ApplicationSharedPtr app_moved_to_full;
  NiceMock<application_manager_test::MockApplication>* app_moved_to_full_mock;

  app_in_full = ConfigureApp(
      &app_in_full_mock, 1761, NOT_MEDIA, NOT_NAVI, NOT_PROJECTION, NOT_VC);
  app_moved_to_full = ConfigureApp(&app_moved_to_full_mock,
                                   1796,
                                   NOT_MEDIA,
                                   NOT_NAVI,
                                   NOT_PROJECTION,
                                   NOT_VC);

  InsertApplication(app_in_full);
  InsertApplication(app_moved_to_full);

  ExpectSuccesfullSetHmiState(app_moved_to_full,
                              app_moved_to_full_mock,
                              BackgroundState(),
                              FullState());

  ExpectAppChangeHmiStateDueToConflictResolving(
      app_in_full, app_in_full_mock, FullState(), BackgroundState());

  state_ctrl_->SetRegularState(
      app_moved_to_full, FullState(), /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest, DISABLED_SetFullToSimpleAppWhileAudioAppIsInFull) {
  am::ApplicationSharedPtr app_in_full = media_navi_vc_app_;
  NiceMock<application_manager_test::MockApplication>* app_in_full_mock =
      media_navi_vc_app_ptr_;

  am::ApplicationSharedPtr app_moved_to_full = simple_app_;
  NiceMock<application_manager_test::MockApplication>* app_moved_to_full_mock =
      simple_app_ptr_;

  InsertApplication(app_in_full);
  InsertApplication(app_moved_to_full);

  ExpectSuccesfullSetHmiState(app_moved_to_full,
                              app_moved_to_full_mock,
                              NoneState(),
                              FullState());

  ExpectAppChangeHmiStateDueToConflictResolving(
      app_in_full, app_in_full_mock, FullAudibleState(), LimitedAudibleState());
  state_ctrl_->SetRegularState(
      app_moved_to_full, FullState(), /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       SetFullToAudioAppWhileAnotherTypeAudioAppIsInFull) {
  am::ApplicationSharedPtr app_in_full = media_app_;
  NiceMock<application_manager_test::MockApplication>* app_in_full_mock =
      media_app_ptr_;

  am::ApplicationSharedPtr app_moved_to_full = navi_app_;
  NiceMock<application_manager_test::MockApplication>* app_moved_to_full_mock =
      navi_app_ptr_;

  InsertApplication(app_in_full);
  InsertApplication(app_moved_to_full);

  ExpectSuccesfullSetHmiState(app_moved_to_full,
                              app_moved_to_full_mock,
                              BackgroundState(),
                              FullAudibleState());

  ExpectAppChangeHmiStateDueToConflictResolving(
      app_in_full, app_in_full_mock, FullAudibleState(), LimitedAudibleState());
  state_ctrl_->SetRegularState(
      app_moved_to_full, FullAudibleState(), /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       SetFullToAudioAppWhileSameTypeAudioAppIsInFull) {
  NiceMock<application_manager_test::MockApplication>* app_in_full_mock;
  am::ApplicationSharedPtr app_in_full = ConfigureApp(
      &app_in_full_mock, 1761, MEDIA, NOT_NAVI, NOT_PROJECTION, NOT_VC);

  NiceMock<application_manager_test::MockApplication>* app_moved_to_full_mock;
  am::ApplicationSharedPtr app_moved_to_full = ConfigureApp(
      &app_moved_to_full_mock, 1796, MEDIA, NOT_NAVI, NOT_PROJECTION, NOT_VC);

  InsertApplication(app_in_full);
  InsertApplication(app_moved_to_full);
  ExpectSuccesfullSetHmiState(app_moved_to_full,
                              app_moved_to_full_mock,
                              BackgroundState(),
                              FullAudibleState());

  ExpectAppChangeHmiStateDueToConflictResolving(
      app_in_full, app_in_full_mock, FullAudibleState(), BackgroundState());

  state_ctrl_->SetRegularState(app_moved_to_full, FullAudibleState(), false);
}

TEST_F(StateControllerImplTest,
       SetFullToAudioAppWhileSameTypeAudioAppIsInLimited) {
  NiceMock<application_manager_test::MockApplication>* app_in_limited_mock;
  am::ApplicationSharedPtr app_in_limited = ConfigureApp(
      &app_in_limited_mock, 1761, NOT_MEDIA, NAVI, NOT_PROJECTION, NOT_VC);

  NiceMock<application_manager_test::MockApplication>* app_moved_to_full_mock;
  am::ApplicationSharedPtr app_moved_to_full = ConfigureApp(
      &app_moved_to_full_mock, 1796, NOT_MEDIA, NAVI, NOT_PROJECTION, VC);

  InsertApplication(app_in_limited);
  InsertApplication(app_moved_to_full);
  ExpectSuccesfullSetHmiState(app_moved_to_full,
                              app_moved_to_full_mock,
                              BackgroundState(),
                              FullAudibleState());

  ExpectAppChangeHmiStateDueToConflictResolving(app_in_limited,
                                                app_in_limited_mock,
                                                LimitedAudibleState(),
                                                BackgroundState());

  state_ctrl_->SetRegularState(
      app_moved_to_full, FullAudibleState(), /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       SetLimitedToAudioAppWhileSameTypeAudioAppIsInLimited) {
  NiceMock<application_manager_test::MockApplication>* app_in_limited_mock;
  am::ApplicationSharedPtr app_in_limited = ConfigureApp(
      &app_in_limited_mock, 1761, NOT_MEDIA, NOT_NAVI, NOT_PROJECTION, VC);

  NiceMock<application_manager_test::MockApplication>*
      app_moved_to_limited_mock;
  am::ApplicationSharedPtr app_moved_to_limited =
      ConfigureApp(&app_moved_to_limited_mock,
                   1796,
                   NOT_MEDIA,
                   NOT_NAVI,
                   NOT_PROJECTION,
                   VC);

  InsertApplication(app_in_limited);
  InsertApplication(app_moved_to_limited);

  ExpectSuccesfullSetHmiState(app_moved_to_limited,
                              app_moved_to_limited_mock,
                              BackgroundState(),
                              LimitedAudibleState());

  ExpectAppChangeHmiStateDueToConflictResolving(app_in_limited,
                                                app_in_limited_mock,
                                                LimitedAudibleState(),
                                                BackgroundState());

  state_ctrl_->SetRegularState(app_moved_to_limited,
                               LimitedAudibleState(),
                               /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       DISABLED_SetLimitedToAudioAppWhileOtherTypeAudioAppIsInLimited) {
  am::ApplicationSharedPtr app_in_limited = navi_app_;
  NiceMock<application_manager_test::MockApplication>* app_in_limited_mock =
      navi_app_ptr_;

  am::ApplicationSharedPtr app_moved_to_limited = vc_app_;
  NiceMock<application_manager_test::MockApplication>*
      app_moved_to_limited_mock = vc_app_ptr_;

  InsertApplication(app_in_limited);
  InsertApplication(app_moved_to_limited);
  ExpectSuccesfullSetHmiState(app_moved_to_limited,
                              app_moved_to_limited_mock,
                              NoneState(),
                              LimitedAudibleState());
  ExpectAppWontChangeHmiStateDueToConflictResolving(
      app_in_limited, app_in_limited_mock, LimitedAudibleState());

  state_ctrl_->SetRegularState(app_moved_to_limited,
                               LimitedAudibleState(),
                               /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       DISABLED_SetLimitedToAudioAppWhileOtherTypeAudioAppIsInFull) {
  am::ApplicationSharedPtr app_in_full = navi_app_;
  NiceMock<application_manager_test::MockApplication>* app_in_full_mock =
      navi_app_ptr_;

  am::ApplicationSharedPtr app_moved_to_limited = vc_app_;
  NiceMock<application_manager_test::MockApplication>*
      app_moved_to_limited_mock = vc_app_ptr_;

  InsertApplication(app_in_full);
  InsertApplication(app_moved_to_limited);

  ExpectSuccesfullSetHmiState(app_moved_to_limited,
                              app_moved_to_limited_mock,
                              NoneState(),
                              LimitedAudibleState());

  ExpectAppWontChangeHmiStateDueToConflictResolving(
      app_in_full, app_in_full_mock, FullAudibleState());
  state_ctrl_->SetRegularState(app_moved_to_limited,
                               LimitedAudibleState(),
                               /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       DISABLED_SetFullToSimpleAppWhile2AudioAppsInLimited) {
  namespace HMILevel = mobile_apis::HMILevel;
  namespace AudioStreamingState = mobile_apis::AudioStreamingState;
  namespace SystemContext = mobile_apis::SystemContext;

  am::ApplicationSharedPtr app_moved_to_full = simple_app_;
  NiceMock<application_manager_test::MockApplication>* app_moved_to_full_mock =
      simple_app_ptr_;

  am::ApplicationSharedPtr limited_app1 = media_app_;
  NiceMock<application_manager_test::MockApplication>* limited_app1_mock =
      media_app_ptr_;

  am::ApplicationSharedPtr limited_app2 = navi_vc_app_;
  NiceMock<application_manager_test::MockApplication>* limited_app2_mock =
      navi_vc_app_ptr_;

  InsertApplication(app_moved_to_full);
  InsertApplication(limited_app1);
  InsertApplication(limited_app2);

  ExpectSuccesfullSetHmiState(app_moved_to_full,
                              app_moved_to_full_mock,
                              NoneState(),
                              FullState());

  ExpectAppWontChangeHmiStateDueToConflictResolving(
      limited_app1, limited_app1_mock, LimitedAudibleState());
  ExpectAppWontChangeHmiStateDueToConflictResolving(
      limited_app2, limited_app2_mock, LimitedAudibleState());

  state_ctrl_->SetRegularState(
      app_moved_to_full, FullState(), /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       DISABLED_SetFullToSimpleAppWhile1AudioAppInLimitedAnd1AudioAppInFull) {
  namespace HMILevel = mobile_apis::HMILevel;
  namespace AudioStreamingState = mobile_apis::AudioStreamingState;
  namespace SystemContext = mobile_apis::SystemContext;

  am::ApplicationSharedPtr app_moved_to_full = simple_app_;
  NiceMock<application_manager_test::MockApplication>* app_moved_to_full_mock =
      simple_app_ptr_;

  am::ApplicationSharedPtr limited_app = media_app_;
  NiceMock<application_manager_test::MockApplication>* limited_app_mock =
      media_app_ptr_;

  am::ApplicationSharedPtr full_app = navi_vc_app_;
  NiceMock<application_manager_test::MockApplication>* full_app_mock =
      navi_vc_app_ptr_;

  InsertApplication(app_moved_to_full);
  InsertApplication(limited_app);
  InsertApplication(full_app);

  ExpectSuccesfullSetHmiState(app_moved_to_full,
                              app_moved_to_full_mock,
                              NoneState(),
                              FullState());

  ExpectAppWontChangeHmiStateDueToConflictResolving(
      limited_app, limited_app_mock, LimitedAudibleState());

  ExpectAppChangeHmiStateDueToConflictResolving(
      full_app, full_app_mock, FullAudibleState(), LimitedAudibleState());

  state_ctrl_->SetRegularState(
      app_moved_to_full, FullState(), /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       SetFullToSimpleAppWhile1AudioAppInLimitedAnd1SimpleAppInFull) {
  namespace HMILevel = mobile_apis::HMILevel;
  namespace AudioStreamingState = mobile_apis::AudioStreamingState;
  namespace SystemContext = mobile_apis::SystemContext;

  NiceMock<application_manager_test::MockApplication>* app_moved_to_full_mock;
  am::ApplicationSharedPtr app_moved_to_full =
      ConfigureApp(&app_moved_to_full_mock,
                   1761,
                   NOT_MEDIA,
                   NOT_NAVI,
                   NOT_PROJECTION,
                   NOT_VC);

  am::ApplicationSharedPtr limited_app = media_app_;
  NiceMock<application_manager_test::MockApplication>* limited_app_mock =
      media_app_ptr_;

  NiceMock<application_manager_test::MockApplication>* full_app_mock;
  am::ApplicationSharedPtr full_app = ConfigureApp(
      &full_app_mock, 1796, NOT_MEDIA, NOT_NAVI, NOT_PROJECTION, NOT_VC);

  InsertApplication(app_moved_to_full);
  InsertApplication(limited_app);
  InsertApplication(full_app);

  ExpectSuccesfullSetHmiState(app_moved_to_full,
                              app_moved_to_full_mock,
                              BackgroundState(),
                              FullState());

  ExpectAppWontChangeHmiStateDueToConflictResolving(
      limited_app, limited_app_mock, LimitedAudibleState());

  ExpectAppChangeHmiStateDueToConflictResolving(
      full_app, full_app_mock, FullState(), BackgroundState());

  state_ctrl_->SetRegularState(
      app_moved_to_full, FullState(), /* send_activate_app = */ false);
}

TEST_F(
    StateControllerImplTest,
    SetFullToAudioAppWhile1AudioAppWithSameTypeInLimitedAnd1SimpleAppInFull) {
  namespace HMILevel = mobile_apis::HMILevel;
  namespace AudioStreamingState = mobile_apis::AudioStreamingState;
  namespace SystemContext = mobile_apis::SystemContext;

  NiceMock<application_manager_test::MockApplication>* app_moved_to_full_mock;
  am::ApplicationSharedPtr app_moved_to_full = ConfigureApp(
      &app_moved_to_full_mock, 1761, MEDIA, NOT_NAVI, NOT_PROJECTION, NOT_VC);

  NiceMock<application_manager_test::MockApplication>* limited_app_mock;
  am::ApplicationSharedPtr limited_app = ConfigureApp(
      &limited_app_mock, 1762, MEDIA, NOT_NAVI, NOT_PROJECTION, NOT_VC);

  NiceMock<application_manager_test::MockApplication>* full_app_mock;
  am::ApplicationSharedPtr full_app = ConfigureApp(
      &full_app_mock, 1796, NOT_MEDIA, NOT_NAVI, NOT_PROJECTION, NOT_VC);

  InsertApplication(app_moved_to_full);
  InsertApplication(limited_app);
  InsertApplication(full_app);

  ExpectSuccesfullSetHmiState(app_moved_to_full,
                              app_moved_to_full_mock,
                              BackgroundState(),
                              FullAudibleState());

  ExpectAppChangeHmiStateDueToConflictResolving(
      limited_app, limited_app_mock, LimitedAudibleState(), BackgroundState());

  ExpectAppChangeHmiStateDueToConflictResolving(
      full_app, full_app_mock, FullState(), BackgroundState());

  state_ctrl_->SetRegularState(
      app_moved_to_full, FullAudibleState(), /* send_activate_app = */ false);
}

TEST_F(
    StateControllerImplTest,
    DISABLED_SetFullToAudioAppWhileAudioAppWithSameTypeInLimitedAndAudioAppWithOtherTypeInFull) {
  namespace HMILevel = mobile_apis::HMILevel;
  namespace AudioStreamingState = mobile_apis::AudioStreamingState;
  namespace SystemContext = mobile_apis::SystemContext;

  NiceMock<application_manager_test::MockApplication>* app_moved_to_full_mock;
  am::ApplicationSharedPtr app_moved_to_full = ConfigureApp(
      &app_moved_to_full_mock, 1761, MEDIA, NOT_NAVI, NOT_PROJECTION, NOT_VC);

  NiceMock<application_manager_test::MockApplication>* limited_app_mock;
  am::ApplicationSharedPtr limited_app = ConfigureApp(
      &limited_app_mock, 1762, MEDIA, NOT_NAVI, NOT_PROJECTION, NOT_VC);

  NiceMock<application_manager_test::MockApplication>* full_app_mock;
  am::ApplicationSharedPtr full_app = ConfigureApp(
      &full_app_mock, 1796, NOT_MEDIA, NAVI, NOT_PROJECTION, NOT_VC);

  InsertApplication(app_moved_to_full);
  InsertApplication(limited_app);
  InsertApplication(full_app);

  ExpectSuccesfullSetHmiState(app_moved_to_full,
                              app_moved_to_full_mock,
                              BackgroundState(),
                              FullAudibleState());

  ExpectAppChangeHmiStateDueToConflictResolving(
      limited_app, limited_app_mock, LimitedAudibleState(), BackgroundState());

  ExpectAppChangeHmiStateDueToConflictResolving(
      full_app, full_app_mock, FullAudibleState(), LimitedAudibleState());

  state_ctrl_->SetRegularState(
      app_moved_to_full, FullAudibleState(), /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       SetFullToAudioAppWhile3AudioAppsWithSameTypeInLimited) {
  namespace HMILevel = mobile_apis::HMILevel;
  namespace AudioStreamingState = mobile_apis::AudioStreamingState;
  namespace SystemContext = mobile_apis::SystemContext;

  InsertApplication(media_navi_vc_app_);
  InsertApplication(media_app_);
  InsertApplication(navi_app_);
  InsertApplication(vc_app_);
  ExpectSuccesfullSetHmiState(media_navi_vc_app_,
                              media_navi_vc_app_ptr_,
                              BackgroundState(),
                              FullAudibleState());

  EXPECT_CALL(*media_app_ptr_, RegularHmiState())
      .WillOnce(Return(LimitedAudibleState()));

  ExpectAppChangeHmiStateDueToConflictResolving(
      navi_app_, navi_app_ptr_, LimitedAudibleState(), BackgroundState());
  ExpectAppChangeHmiStateDueToConflictResolving(
      vc_app_, vc_app_ptr_, LimitedAudibleState(), BackgroundState());
  state_ctrl_->SetRegularState(
      media_navi_vc_app_, FullAudibleState(), /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       SetFullToAudioAppWhile2AudioAppsWithSameTypeInLimitedAndOneInFull) {
  namespace HMILevel = mobile_apis::HMILevel;
  namespace AudioStreamingState = mobile_apis::AudioStreamingState;
  namespace SystemContext = mobile_apis::SystemContext;

  InsertApplication(media_navi_vc_app_);
  InsertApplication(media_app_);
  InsertApplication(navi_app_);
  InsertApplication(vc_app_);
  ExpectSuccesfullSetHmiState(media_navi_vc_app_,
                              media_navi_vc_app_ptr_,
                              BackgroundState(),
                              FullAudibleState());

  EXPECT_CALL(*media_app_ptr_, RegularHmiState())
      .WillOnce(Return(LimitedAudibleState()));

  ExpectAppChangeHmiStateDueToConflictResolving(
      navi_app_, navi_app_ptr_, LimitedAudibleState(), BackgroundState());
  ExpectAppChangeHmiStateDueToConflictResolving(
      vc_app_, vc_app_ptr_, FullAudibleState(), BackgroundState());
  state_ctrl_->SetRegularState(
      media_navi_vc_app_, FullAudibleState(), /* send_activate_app = */ false);
}

// TODO {AKozoriz} Changed logic in state_controller
TEST_F(StateControllerImplTest, DISABLED_ActivateAppSuccessReceivedFromHMI) {
  using namespace hmi_apis;
  using namespace mobile_apis;

  const uint32_t corr_id = 314;
  const uint32_t hmi_app_id = 2718;
  typedef std::pair<am::HmiStatePtr, Common_HMILevel::eType> StateLevelPair;
  std::vector<StateLevelPair> hmi_states;
  hmi_states.push_back(
      StateLevelPair(FullAudibleState(), Common_HMILevel::FULL));
  hmi_states.push_back(StateLevelPair(FullState(), Common_HMILevel::FULL));
  hmi_states.push_back(
      StateLevelPair(LimitedAudibleState(), Common_HMILevel::LIMITED));
  hmi_states.push_back(
      StateLevelPair(BackgroundState(), Common_HMILevel::BACKGROUND));
  hmi_states.push_back(
      StateLevelPair(CreateHmiState(HMILevel::HMI_NONE,
                                    AudioStreamingState::NOT_AUDIBLE,
                                    VideoStreamingState::NOT_STREAMABLE,
                                    SystemContext::SYSCTXT_MAIN),
                     Common_HMILevel::NONE));
  std::vector<StateLevelPair> initial_hmi_states = hmi_states;
  std::vector<StateLevelPair>::iterator it = hmi_states.begin();
  std::vector<StateLevelPair>::iterator it2 = initial_hmi_states.begin();
  smart_objects::SmartObjectSPtr bc_activate_app_request =
      new smart_objects::SmartObject();
  (*bc_activate_app_request)[am::strings::params][am::strings::correlation_id] =
      corr_id;

  for (; it != hmi_states.end(); ++it) {
    am::HmiStatePtr hmi_state = it->first;
    am::HmiStatePtr initial_hmi_state = it2->first;
    Common_HMILevel::eType hmi_level = it->second;

    SetBCActivateAppRequestToHMI(hmi_level, corr_id);
    ON_CALL(app_manager_mock_, ManageHMICommand(bc_activate_app_request))
        .WillByDefault(Return(true));

    EXPECT_CALL(app_manager_mock_, application_id(corr_id))
        .WillOnce(Return(hmi_app_id));
    EXPECT_CALL(app_manager_mock_, application_by_hmi_app(hmi_app_id))
        .WillOnce(Return(media_app_));
    ExpectSuccesfullSetHmiState(
        media_app_, media_app_ptr_, initial_hmi_state, hmi_state);
    state_ctrl_->SetRegularState(
        media_app_, hmi_state, /* send_activate_app = */ true);
    smart_objects::SmartObject message;
    message[am::strings::params][am::hmi_response::code] =
        Common_Result::SUCCESS;
    message[am::strings::params][am::strings::correlation_id] = corr_id;
    am::event_engine::Event event(
        hmi_apis::FunctionID::BasicCommunication_ActivateApp);
    event.set_smart_object(message);
    state_ctrl_->on_event(event);
  }
}

std::vector<hmi_apis::Common_Result::eType> hmi_result() {
  using namespace hmi_apis;
  std::vector<Common_Result::eType> hmi_results;
  hmi_results.push_back(Common_Result::ABORTED);
  hmi_results.push_back(Common_Result::APPLICATION_NOT_REGISTERED);
  hmi_results.push_back(Common_Result::CHAR_LIMIT_EXCEEDED);
  hmi_results.push_back(Common_Result::DATA_NOT_AVAILABLE);
  hmi_results.push_back(Common_Result::DISALLOWED);
  hmi_results.push_back(Common_Result::DUPLICATE_NAME);
  hmi_results.push_back(Common_Result::GENERIC_ERROR);
  hmi_results.push_back(Common_Result::IGNORED);
  hmi_results.push_back(Common_Result::INVALID_DATA);
  hmi_results.push_back(Common_Result::INVALID_ENUM);
  hmi_results.push_back(Common_Result::INVALID_ID);
  hmi_results.push_back(Common_Result::IN_USE);
  hmi_results.push_back(Common_Result::NO_APPS_REGISTERED);
  hmi_results.push_back(Common_Result::NO_DEVICES_CONNECTED);
  hmi_results.push_back(Common_Result::OUT_OF_MEMORY);
  hmi_results.push_back(Common_Result::REJECTED);
  hmi_results.push_back(Common_Result::RETRY);
  hmi_results.push_back(Common_Result::TIMED_OUT);
  hmi_results.push_back(Common_Result::TOO_MANY_PENDING_REQUESTS);
  hmi_results.push_back(Common_Result::TRUNCATED_DATA);
  hmi_results.push_back(Common_Result::UNSUPPORTED_REQUEST);
  hmi_results.push_back(Common_Result::UNSUPPORTED_RESOURCE);
  hmi_results.push_back(Common_Result::USER_DISALLOWED);
  hmi_results.push_back(Common_Result::WARNINGS);
  hmi_results.push_back(Common_Result::WRONG_LANGUAGE);
  return hmi_results;
}

TEST_F(StateControllerImplTest, SendEventBCActivateApp_HMIReceivesError) {
  using namespace hmi_apis;
  const uint32_t corr_id = 314;
  const uint32_t hmi_app_id = 2718;
  const std::vector<Common_Result::eType> hmi_results = hmi_result();

  for (Common_Result::eType result : hmi_results) {
    EXPECT_CALL(app_manager_mock_, application_id(corr_id))
        .WillOnce(Return(hmi_app_id));
    EXPECT_CALL(app_manager_mock_, application_by_hmi_app(hmi_app_id))
        .WillOnce(Return(simple_app_));

    EXPECT_CALL(*simple_app_ptr_, RegularHmiState()).Times(0);
    EXPECT_CALL(*simple_app_ptr_, CurrentHmiState()).Times(0);
    EXPECT_CALL(*simple_app_ptr_, SetRegularState(_)).Times(0);

    EXPECT_CALL(app_manager_mock_, SendHMIStatusNotification(simple_app_))
        .Times(0);
    EXPECT_CALL(app_manager_mock_,
                OnHMILevelChanged(simple_app_->app_id(), _, _)).Times(0);

    smart_objects::SmartObject message;
    message[am::strings::params][am::hmi_response::code] = result;
    message[am::strings::params][am::strings::correlation_id] = corr_id;
    am::event_engine::Event event(FunctionID::BasicCommunication_ActivateApp);
    event.set_smart_object(message);
    state_ctrl_->on_event(event);
  }
}

TEST_F(StateControllerImplTest, ActivateAppInvalidCorrelationId) {
  using namespace hmi_apis;
  const uint32_t corr_id = 314;
  const uint32_t hmi_app_id = 2718;

  EXPECT_CALL(app_manager_mock_, application_id(corr_id))
      .WillOnce(Return(hmi_app_id));
  EXPECT_CALL(app_manager_mock_, application_by_hmi_app(hmi_app_id))
      .WillOnce(Return(am::ApplicationSharedPtr()));
  EXPECT_CALL(*simple_app_ptr_, SetRegularState(_)).Times(0);
  EXPECT_CALL(app_manager_mock_, SendHMIStatusNotification(simple_app_))
      .Times(0);
  EXPECT_CALL(app_manager_mock_, OnHMILevelChanged(simple_app_->app_id(), _, _))
      .Times(0);
  SetBCActivateAppRequestToHMI(Common_HMILevel::FULL, corr_id);
  state_ctrl_->SetRegularState(
      simple_app_, FullState(), /* send_activate_app = */ true);
  smart_objects::SmartObject message;
  message[am::strings::params][am::hmi_response::code] = Common_Result::SUCCESS;
  message[am::strings::params][am::strings::correlation_id] = corr_id;
  am::event_engine::Event event(FunctionID::BasicCommunication_ActivateApp);
  event.set_smart_object(message);
  state_ctrl_->on_event(event);
}

TEST_F(StateControllerImplTest, ApplyTempStatesForSimpleApp) {
  InsertApplication(simple_app_);
  CheckStateApplyingForApplication(
      simple_app_, *simple_app_ptr_, valid_state_ids_);
}

TEST_F(StateControllerImplTest, ApplyTempStatesForMediaApp) {
  InsertApplication(media_app_);
  CheckStateApplyingForApplication(
      media_app_, *media_app_ptr_, valid_state_ids_);
}

TEST_F(StateControllerImplTest, ApplyTempStatesForNaviApp) {
  InsertApplication(navi_app_);
  CheckStateApplyingForApplication(
      navi_app_, *navi_app_ptr_, navi_valid_state_ids_);
}

TEST_F(StateControllerImplTest, ApplyTempStatesForVCApp) {
  InsertApplication(vc_app_);
  CheckStateApplyingForApplication(vc_app_, *vc_app_ptr_, valid_state_ids_);
}

TEST_F(StateControllerImplTest, ApplyTempStatesForMediaNaviApp) {
  InsertApplication(media_navi_app_);
  CheckStateApplyingForApplication(
      media_navi_app_, *media_navi_app_ptr_, navi_valid_state_ids_);
}

TEST_F(StateControllerImplTest, ApplyTempStatesForMediaVCApp) {
  InsertApplication(media_vc_app_);
  CheckStateApplyingForApplication(
      media_vc_app_, *media_vc_app_ptr_, valid_state_ids_);
}

TEST_F(StateControllerImplTest, ApplyTempStatesForNaviVCApp) {
  InsertApplication(navi_vc_app_);
  CheckStateApplyingForApplication(
      navi_vc_app_, *navi_vc_app_ptr_, navi_valid_state_ids_);
}

TEST_F(StateControllerImplTest, ApplyTempStatesForMediaNaviVCApp) {
  InsertApplication(media_navi_vc_app_);
  CheckStateApplyingForApplication(
      media_navi_vc_app_, *media_navi_vc_app_ptr_, navi_valid_state_ids_);
}

TEST_F(StateControllerImplTest, SetStatePhoneCallForNonMediaApplication) {
  am::HmiStatePtr state_phone_call =
      utils::MakeShared<am::PhoneCallHmiState>(simple_app_, app_manager_mock_);
  TestSetState(simple_app_,
               state_phone_call,
               APP_TYPE_NON_MEDIA,
               &StateControllerImplTest::PreparePhoneCallHMIStateResults);
}

TEST_F(StateControllerImplTest, SetStatePhoneCallForMediaApplication) {
  am::HmiStatePtr state_phone_call =
      utils::MakeShared<am::PhoneCallHmiState>(media_app_, app_manager_mock_);
  TestSetState(media_app_,
               state_phone_call,
               APP_TYPE_MEDIA,
               &StateControllerImplTest::PreparePhoneCallHMIStateResults);
}

TEST_F(StateControllerImplTest, SetStatePhoneCallForMediaNaviApplication) {
  am::HmiStatePtr state_phone_call = utils::MakeShared<am::PhoneCallHmiState>(
      media_navi_app_, app_manager_mock_);
  TestSetState(media_navi_app_,
               state_phone_call,
               APP_TYPE_NAVI,
               &StateControllerImplTest::PreparePhoneCallHMIStateResults);
}

TEST_F(StateControllerImplTest, SetVRStateForNonMediaApplication) {
  am::HmiStatePtr state_vr =
      utils::MakeShared<am::VRHmiState>(simple_app_, app_manager_mock_);
  TestSetState(simple_app_,
               state_vr,
               APP_TYPE_NON_MEDIA,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, SetVRStateForMediaApplication) {
  am::HmiStatePtr state_vr =
      utils::MakeShared<am::VRHmiState>(media_app_, app_manager_mock_);
  TestSetState(media_app_,
               state_vr,
               APP_TYPE_MEDIA,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, SetVRStateForMediaNaviVoiceApplication) {
  am::HmiStatePtr state_vr =
      utils::MakeShared<am::VRHmiState>(media_navi_vc_app_, app_manager_mock_);
  TestSetState(media_navi_vc_app_,
               state_vr,
               APP_TYPE_MEDIA,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest,
       SetTTSStateForNonMediaApplicationAttenuatedNotSupported) {
  am::HmiStatePtr state_tts =
      utils::MakeShared<am::TTSHmiState>(simple_app_, app_manager_mock_);
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(false));
  TestSetState(simple_app_,
               state_tts,
               APP_TYPE_NON_MEDIA,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest,
       SetTTSStateForNonMediaApplicationAttenuatedSupported) {
  am::HmiStatePtr state_tts =
      utils::MakeShared<am::TTSHmiState>(simple_app_, app_manager_mock_);
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(true));
  TestSetState(simple_app_,
               state_tts,
               APP_TYPE_NON_MEDIA,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest,
       SetTTSStateForMediaApplicationAttenuatedNotSupported) {
  am::HmiStatePtr state_tts =
      utils::MakeShared<am::TTSHmiState>(media_app_, app_manager_mock_);
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(false));
  TestSetState(media_app_,
               state_tts,
               APP_TYPE_MEDIA,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest,
       SetTTSStateForMediaApplicationAttenuatedSupported) {
  am::HmiStatePtr state_tts =
      utils::MakeShared<am::TTSHmiState>(media_app_, app_manager_mock_);
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(true));
  TestSetState(media_app_,
               state_tts,
               APP_TYPE_ATTENUATED,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest,
       SetTTSStateForMediaNaviVCApplicationAttenuatedNotSupported) {
  am::HmiStatePtr state_tts =
      utils::MakeShared<am::TTSHmiState>(media_navi_vc_app_, app_manager_mock_);
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(false));
  TestSetState(media_navi_vc_app_,
               state_tts,
               APP_TYPE_MEDIA,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest,
       SetTTSStateForMediaNaviVCApplicationAttenuatedSupported) {
  am::HmiStatePtr state_tts =
      utils::MakeShared<am::TTSHmiState>(media_navi_vc_app_, app_manager_mock_);
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(true));
  TestSetState(media_navi_vc_app_,
               state_tts,
               APP_TYPE_ATTENUATED,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, SetNaviStreamingStateForNonMediaApplication) {
  am::HmiStatePtr state_navi_streming =
      utils::MakeShared<am::NaviStreamingHmiState>(simple_app_,
                                                    app_manager_mock_);
  TestSetState(simple_app_,
               state_navi_streming,
               APP_TYPE_NON_MEDIA,
               &StateControllerImplTest::PrepareVideoStreamingHmiStateResults);
}

TEST_F(StateControllerImplTest,
       SetNaviStreamingStateMediaApplicationAttenuatedNotSupported) {
  am::HmiStatePtr state_navi_streming =
      utils::MakeShared<am::NaviStreamingHmiState>(media_app_,
                                                   app_manager_mock_);
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(false));
  TestSetState(media_app_,
               state_navi_streming,
               APP_TYPE_MEDIA,
               &StateControllerImplTest::PrepareVideoStreamingHmiStateResults);
}

TEST_F(StateControllerImplTest,
       SetNaviStreamingStateMediaApplicationAttenuatedSupported) {
  am::HmiStatePtr state_navi_streming =
      utils::MakeShared<am::NaviStreamingHmiState>(media_app_,
                                                   app_manager_mock_);
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(true));
  TestSetState(media_app_,
               state_navi_streming,
               APP_TYPE_ATTENUATED,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest,
       SetNaviStreamingStateVCApplicationAttenuatedNotSupported) {
  am::HmiStatePtr state_navi_streming =
      utils::MakeShared<am::NaviStreamingHmiState>(vc_app_, app_manager_mock_);
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(false));
  TestSetState(vc_app_,
               state_navi_streming,
               APP_TYPE_MEDIA,
               &StateControllerImplTest::PrepareVideoStreamingHmiStateResults);
}

TEST_F(StateControllerImplTest,
       SetNaviStreamingStateVCApplicationAttenuatedSupported) {
  am::HmiStatePtr state_navi_streming =
      utils::MakeShared<am::NaviStreamingHmiState>(vc_app_, app_manager_mock_);
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(true));
  TestSetState(vc_app_,
               state_navi_streming,
               APP_TYPE_ATTENUATED,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, SetNaviStreamingStateNaviApplication) {
  am::HmiStatePtr state_navi_streming =
      utils::MakeShared<am::NaviStreamingHmiState>(navi_app_,
                                                    app_manager_mock_);
  TestSetState(navi_app_,
               state_navi_streming,
               APP_TYPE_NAVI,
               &StateControllerImplTest::PrepareVideoStreamingHmiStateResults);
}

TEST_F(StateControllerImplTest,
       SetNaviStreamingStateMediaNaviApplication) {
  am::HmiStatePtr state_navi_streming =
      utils::MakeShared<am::NaviStreamingHmiState>(media_navi_app_,
                                                    app_manager_mock_);
  TestSetState(media_navi_app_,
               state_navi_streming,
               APP_TYPE_NAVI,
               &StateControllerImplTest::PrepareVideoStreamingHmiStateResults);
}

TEST_F(StateControllerImplTest, SetSafetyModeStateForNonMediaApplication) {
  am::HmiStatePtr state_safety_mode =
      utils::MakeShared<am::SafetyModeHmiState>(simple_app_, app_manager_mock_);
  TestSetState(simple_app_,
               state_safety_mode,
               APP_TYPE_NON_MEDIA,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, SetSafetyModeStateForMediaApplication) {
  am::HmiStatePtr state_safety_mode =
      utils::MakeShared<am::VRHmiState>(media_app_, app_manager_mock_);
  TestSetState(media_app_,
               state_safety_mode,
               APP_TYPE_MEDIA,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest,
       SetSafetyModeStateForMediaNaviVoiceApplication) {
  am::HmiStatePtr state_safety_mode =
      utils::MakeShared<am::VRHmiState>(media_navi_vc_app_, app_manager_mock_);
  TestSetState(media_navi_vc_app_,
               state_safety_mode,
               APP_TYPE_MEDIA,
               &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, DISABLED_MixVRWithPhoneCall) {
  TestMixState<am::PhoneCallHmiState, am::VRHmiState>(
      &StateControllerImplTest::PreparePhoneCallHMIStateResults);
}

TEST_F(StateControllerImplTest,
       DISABLED_MixTTSWithPhoneCallAttenuatedNotSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(false));

  TestMixState<am::PhoneCallHmiState, am::TTSHmiState>(
      &StateControllerImplTest::PreparePhoneCallHMIStateResults);
}

TEST_F(StateControllerImplTest,
       DISABLED_MixTTSWithPhoneCallAttenuatedSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(true));

  TestMixState<am::PhoneCallHmiState, am::TTSHmiState>(
      &StateControllerImplTest::PreparePhoneCallHMIStateResults);
}

TEST_F(StateControllerImplTest,
       DISABLED_MixNaviStreamingWithPhoneCallAttenuatedNotSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(false));

  TestMixState<am::PhoneCallHmiState, am::NaviStreamingHmiState>(
      &StateControllerImplTest::PreparePhoneCallHMIStateResults);
}

TEST_F(StateControllerImplTest,
       DISABLED_MixNaviStreamingWithPhoneCallAttenuatedSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(true));

  TestMixState<am::PhoneCallHmiState, am::NaviStreamingHmiState>(
      &StateControllerImplTest::PreparePhoneCallHMIStateResults);
}

TEST_F(StateControllerImplTest, DISABLED_MixSafetyModeWithPhoneCall) {
  TestMixState<am::PhoneCallHmiState, am::SafetyModeHmiState>(
      &StateControllerImplTest::PreparePhoneCallHMIStateResults);
}

TEST_F(StateControllerImplTest, MixTTSWithVRAttenuatedNotSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(false));

  TestMixState<am::VRHmiState, am::TTSHmiState>(
      &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, MixTTSWithVRAttenuatedSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(true));

  TestMixState<am::VRHmiState, am::TTSHmiState>(
      &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, MixNaviStreamingWithVRAttenuatedNotSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(false));

  TestMixState<am::VRHmiState, am::NaviStreamingHmiState>(
      &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, MixNaviStreamingWithVRAttenuatedSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(true));

  TestMixState<am::VRHmiState, am::NaviStreamingHmiState>(
      &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, MixSafetyModeStreamingWithVR) {
  TestMixState<am::VRHmiState, am::SafetyModeHmiState>(
      &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest,
       MixNaviStreamingWithTTSAttenueatedNotSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(false));

  TestMixState<am::TTSHmiState, am::NaviStreamingHmiState>(
      &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, MixNaviStreamingWithTTSAttenueatedSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(true));

  TestMixState<am::TTSHmiState, am::NaviStreamingHmiState>(
      &StateControllerImplTest::PrepareNaviStreamTTSStateResult);
}

TEST_F(StateControllerImplTest, MixSafetyModeWithTTSAttenueatedNotSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(false));

  TestMixState<am::TTSHmiState, am::SafetyModeHmiState>(
      &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, MixSafetyModeWithTTSAttenueatedSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(true));

  TestMixState<am::TTSHmiState, am::SafetyModeHmiState>(
      &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest,
       MixSafetyModeWithNaviStreamingAttenueatedNotSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(false));

  TestMixState<am::SafetyModeHmiState, am::NaviStreamingHmiState>(
      &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest,
       MixSafetyModeWithNaviStreamingAttenueatedSupported) {
  EXPECT_CALL(app_manager_mock_, is_attenuated_supported())
      .WillRepeatedly(Return(true));

  TestMixState<am::SafetyModeHmiState, am::NaviStreamingHmiState>(
      &StateControllerImplTest::PrepareVRTTSHMIStateResults);
}

TEST_F(StateControllerImplTest, SetRegularStateWithNewHmiLvl) {
  using namespace mobile_apis;

  HMILevel::eType set_lvl = HMILevel::HMI_NONE;
  EXPECT_CALL(*simple_app_ptr_, RegularHmiState())
      .WillOnce(Return(BackgroundState()));

  EXPECT_CALL(*simple_app_ptr_, CurrentHmiState())
      .WillOnce(Return(BackgroundState()))
      .WillOnce(Return(BackgroundState()));

  state_ctrl_->SetRegularState(simple_app_, set_lvl);

  set_lvl = HMILevel::HMI_LIMITED;
  EXPECT_CALL(*simple_app_ptr_, RegularHmiState())
      .WillOnce(Return(BackgroundState()));

  EXPECT_CALL(*simple_app_ptr_, CurrentHmiState())
      .WillOnce(Return(BackgroundState()))
      .WillOnce(Return(BackgroundState()));
  state_ctrl_->SetRegularState(simple_app_, set_lvl);

  set_lvl = HMILevel::HMI_FULL;
  EXPECT_CALL(*simple_app_ptr_, RegularHmiState())
      .WillOnce(Return(BackgroundState()));

  const uint32_t corr_id = 314;
  SetBCActivateAppRequestToHMI(
      static_cast<hmi_apis::Common_HMILevel::eType>(set_lvl), corr_id);

  state_ctrl_->SetRegularState(simple_app_, set_lvl);

  set_lvl = HMILevel::HMI_BACKGROUND;
  EXPECT_CALL(*simple_app_ptr_, RegularHmiState())
      .WillOnce(Return(BackgroundState()));

  EXPECT_CALL(*simple_app_ptr_, CurrentHmiState())
      .WillOnce(Return(BackgroundState()))
      .WillOnce(Return(BackgroundState()));

  state_ctrl_->SetRegularState(simple_app_, set_lvl);
}

TEST_F(StateControllerImplTest, SetRegularStateFullAudibleStreamable) {
  HmiStatePtr check_state = FullAudibleStreamableState();
  EXPECT_CALL(*media_app_ptr_, RegularHmiState())
      .WillRepeatedly(Return(NoneState()));

  EXPECT_CALL(*media_app_ptr_, CurrentHmiState())
      .WillRepeatedly(Return(check_state));
  EXPECT_CALL(*media_app_ptr_,
              SetRegularState(Truly(HmiStatesComparator(check_state))));

  state_ctrl_->SetRegularState(media_app_,
                               check_state->hmi_level(),
                               check_state->audio_streaming_state(),
                               check_state->video_streaming_state(),
                               check_state->system_context());
}

TEST_F(StateControllerImplTest,
       SetRegularStateToMediaAndNonMediaApps_VRStarted_SetPostponedState) {
  using namespace mobile_apis;

  // Precondition
  am::event_engine::Event event(hmi_apis::FunctionID::VR_Started);
  state_ctrl_->on_event(event);

  HmiStatePtr check_state = FullState();

  // Non-media app can't have LIMITED-AUDIO state
  EXPECT_CALL(*simple_app_ptr_, is_resuming()).WillRepeatedly(Return(true));
  EXPECT_CALL(*simple_app_ptr_, CurrentHmiState()).Times(0);
  EXPECT_CALL(*simple_app_ptr_, SetRegularState(_)).Times(0);
  EXPECT_CALL(app_manager_mock_, GetDefaultHmiLevel(_))
      .WillRepeatedly(Return(mobile_apis::HMILevel::HMI_NONE));
  EXPECT_CALL(app_manager_mock_, active_application())
      .WillRepeatedly(Return(am::ApplicationSharedPtr()));
  EXPECT_CALL(*simple_app_ptr_,
              SetPostponedState(Truly(HmiStatesComparator(check_state))));
  state_ctrl_->SetRegularState(
      simple_app_, check_state, /* send_activate_app = */ false);

  check_state = LimitedAudibleState();
  EXPECT_CALL(*media_app_ptr_, is_resuming()).WillRepeatedly(Return(true));
  EXPECT_CALL(*media_app_ptr_, CurrentHmiState()).Times(0);
  EXPECT_CALL(*media_app_ptr_, SetRegularState(_)).Times(0);
  EXPECT_CALL(*media_app_ptr_,
              SetPostponedState(Truly(HmiStatesComparator(check_state))));
  state_ctrl_->SetRegularState(
      media_app_, check_state, /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest, SetRegularStateMediaToNonMediaApp_VR_Stopped) {
  using namespace mobile_apis;

  // Precondition
  am::event_engine::Event prev_event(hmi_apis::FunctionID::VR_Started);
  state_ctrl_->on_event(prev_event);

  am::event_engine::Event next_event(hmi_apis::FunctionID::VR_Stopped);
  state_ctrl_->on_event(next_event);

  // Set state of non-media app after vr has stopped
  HmiStatePtr check_state = FullState();

  // Non-media app can't have LIMITED-AUDIO state
  EXPECT_CALL(*simple_app_ptr_, CurrentHmiState())
      .WillOnce(Return(check_state))
      .WillOnce(Return(check_state));

  EXPECT_CALL(*simple_app_ptr_, is_resuming()).WillRepeatedly(Return(false));

  EXPECT_CALL(message_helper_mock_,
              SendOnResumeAudioSourceToHMI(simple_app_id_, _)).Times(0);
  EXPECT_CALL(*simple_app_ptr_,
              SetPostponedState(Truly(HmiStatesComparator(check_state))))
      .Times(0);
  EXPECT_CALL(*simple_app_ptr_,
              SetRegularState(Truly(HmiStatesComparator(check_state))));
  state_ctrl_->SetRegularState(
      simple_app_, check_state, /* send_activate_app = */ false);

  // Set state of media app after vr has stopped
  check_state = LimitedAudibleState();

  EXPECT_CALL(*media_app_ptr_, CurrentHmiState())
      .WillOnce(Return(check_state))
      .WillOnce(Return(check_state));

  EXPECT_CALL(*media_app_ptr_, is_resuming()).WillRepeatedly(Return(true));

  EXPECT_CALL(message_helper_mock_,
              SendOnResumeAudioSourceToHMI(media_app_id_, _));
  EXPECT_CALL(*media_app_ptr_,
              SetPostponedState(Truly(HmiStatesComparator(check_state))))
      .Times(0);
  EXPECT_CALL(*media_app_ptr_,
              SetRegularState(Truly(HmiStatesComparator(check_state))));
  state_ctrl_->SetRegularState(
      media_app_, check_state, /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       SetRegStateForMediaAndNonMediaApps_OnEmergencyEvent_SetPostponedState) {
  using namespace hmi_apis;
  using namespace smart_objects;
  using namespace am::event_engine;

  // Precondition
  Event event(FunctionID::BasicCommunication_OnEventChanged);
  SmartObject message;
  message[am::strings::msg_params][am::hmi_notification::is_active] = true;
  message[am::strings::msg_params][am::hmi_notification::event_name] =
      Common_EventTypes::EMERGENCY_EVENT;

  event.set_smart_object(message);
  state_ctrl_->on_event(event);

  // Non-media app can't have LIMITED-AUDIO state
  HmiStatePtr check_state = FullState();
  EXPECT_CALL(*simple_app_ptr_, is_resuming()).WillRepeatedly(Return(true));

  EXPECT_CALL(*simple_app_ptr_, RegularHmiState()).Times(0);
  EXPECT_CALL(*simple_app_ptr_, CurrentHmiState()).Times(0);
  EXPECT_CALL(*simple_app_ptr_, SetRegularState(_)).Times(0);
  EXPECT_CALL(app_manager_mock_, GetDefaultHmiLevel(_))
      .WillRepeatedly(Return(mobile_apis::HMILevel::HMI_NONE));
  EXPECT_CALL(app_manager_mock_, active_application())
      .WillRepeatedly(Return(am::ApplicationSharedPtr()));
  EXPECT_CALL(*simple_app_ptr_,
              SetPostponedState(Truly(HmiStatesComparator(check_state))));
  state_ctrl_->SetRegularState(
      simple_app_, check_state, /* send_activate_app = */ false);

  // Set media app
  check_state = LimitedAudibleState();
  EXPECT_CALL(*media_app_ptr_, is_resuming()).WillRepeatedly(Return(true));

  EXPECT_CALL(*media_app_ptr_, RegularHmiState()).Times(0);
  EXPECT_CALL(*media_app_ptr_, CurrentHmiState()).Times(0);
  EXPECT_CALL(*media_app_ptr_, SetRegularState(_)).Times(0);

  EXPECT_CALL(*media_app_ptr_,
              SetPostponedState(Truly(HmiStatesComparator(check_state))));
  state_ctrl_->SetRegularState(
      media_app_, check_state, /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       SetStateForMediaApp_BCOnPhoneCall_SetPostponedState) {
  using namespace hmi_apis;
  using namespace smart_objects;
  using namespace am::event_engine;

  // Precondition
  Event event(FunctionID::BasicCommunication_OnEventChanged);
  SmartObject message;
  message[am::strings::msg_params][am::hmi_notification::is_active] = true;
  message[am::strings::msg_params][am::hmi_notification::event_name] =
      hmi_apis::Common_EventTypes::PHONE_CALL;

  event.set_smart_object(message);
  state_ctrl_->on_event(event);

  am::HmiStatePtr check_state = FullAudibleState();

  EXPECT_CALL(*media_app_ptr_, is_resuming()).WillRepeatedly(Return(true));

  EXPECT_CALL(*media_app_ptr_, is_media_application())
      .WillRepeatedly(Return(true));

  EXPECT_CALL(app_manager_mock_, IsAppTypeExistsInFullOrLimited(_))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(app_manager_mock_, active_application())
      .WillRepeatedly(Return(am::ApplicationSharedPtr()));

  EXPECT_CALL(*media_app_ptr_,
              SetPostponedState(Truly(HmiStatesComparator(check_state))));
  state_ctrl_->SetRegularState(media_app_, check_state, false);
}

TEST_F(StateControllerImplTest,
       SetStateForNaviApp_BCOnPhoneCall_SetPostponedState) {
  using namespace hmi_apis;
  using namespace smart_objects;
  using namespace am::event_engine;

  // Precondition
  Event event(FunctionID::BasicCommunication_OnEventChanged);
  SmartObject message;
  message[am::strings::msg_params][am::hmi_notification::is_active] = true;
  message[am::strings::msg_params][am::hmi_notification::event_name] =
      hmi_apis::Common_EventTypes::PHONE_CALL;

  event.set_smart_object(message);
  state_ctrl_->on_event(event);

  am::HmiStatePtr hmi_state = FullAudibleState();

  EXPECT_CALL(*navi_app_ptr_, is_resuming()).WillRepeatedly(Return(true));

  EXPECT_CALL(*navi_app_ptr_, is_navi()).WillRepeatedly(Return(true));

  EXPECT_CALL(app_manager_mock_, IsAppTypeExistsInFullOrLimited(_))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*navi_app_ptr_, CurrentHmiState())
      .WillRepeatedly(Return(hmi_state));

  EXPECT_CALL(app_manager_mock_, active_application())
      .WillRepeatedly(Return(am::ApplicationSharedPtr()));

  state_ctrl_->SetRegularState(
      navi_app_, hmi_state, /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       SetStateForNaviApp_BCOnPhoneCall_NotPostponedState) {
  using namespace hmi_apis;
  using namespace smart_objects;
  using namespace am::event_engine;

  // Precondition
  Event event(FunctionID::BasicCommunication_OnEventChanged);
  SmartObject message;
  message[am::strings::msg_params][am::hmi_notification::is_active] = false;
  message[am::strings::msg_params][am::hmi_notification::event_name] =
      hmi_apis::Common_EventTypes::PHONE_CALL;

  event.set_smart_object(message);
  state_ctrl_->on_event(event);

  am::HmiStatePtr hmi_state = FullAudibleState();

  EXPECT_CALL(*navi_app_ptr_, is_resuming()).WillRepeatedly(Return(true));

  EXPECT_CALL(*navi_app_ptr_, is_navi()).WillRepeatedly(Return(true));

  EXPECT_CALL(app_manager_mock_, IsAppTypeExistsInFullOrLimited(_))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*navi_app_ptr_, CurrentHmiState())
      .WillRepeatedly(Return(hmi_state));

  EXPECT_CALL(app_manager_mock_, active_application())
      .WillRepeatedly(Return(am::ApplicationSharedPtr()));

  EXPECT_CALL(*navi_app_ptr_, SetPostponedState(_)).Times(0);

  state_ctrl_->SetRegularState(
      navi_app_, hmi_state, /* send_activate_app = */ false);
}

TEST_F(StateControllerImplTest,
       SetStateForNaviApp_BCOnPhoneCall_SetPostponedStateWithActivation) {
  using namespace hmi_apis;
  using namespace smart_objects;
  using namespace am::event_engine;
  using namespace constants;

  const uint32_t app_id = navi_app_->app_id();
  // Precondition
  Event event(FunctionID::BasicCommunication_OnEventChanged);
  SmartObject message;
  message[am::strings::msg_params][am::hmi_notification::is_active] = true;
  message[am::strings::msg_params][am::hmi_notification::event_name] =
      hmi_apis::Common_EventTypes::PHONE_CALL;

  event.set_smart_object(message);
  state_ctrl_->on_event(event);

  am::HmiStatePtr hmi_state = FullAudibleState();

  EXPECT_CALL(*navi_app_ptr_, is_resuming()).WillRepeatedly(Return(true));
  EXPECT_CALL(*navi_app_ptr_, is_navi()).WillRepeatedly(Return(true));
  EXPECT_CALL(*navi_app_ptr_, app_id()).WillRepeatedly(Return(app_id));

  SetBCActivateAppRequestToHMI(hmi_apis::Common_HMILevel::FULL, kCorrID);

  EXPECT_CALL(app_manager_mock_, IsAppTypeExistsInFullOrLimited(_))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*navi_app_ptr_, CurrentHmiState())
      .WillRepeatedly(Return(hmi_state));

  EXPECT_CALL(app_manager_mock_, active_application())
      .WillRepeatedly(Return(am::ApplicationSharedPtr()));

  state_ctrl_->SetRegularState(navi_app_, hmi_state, true);
}

TEST_F(StateControllerImplTest,
       SetStateForNaviApp_BCOnPhoneCall_NotPostponedStateWithActivation) {
  using namespace hmi_apis;
  using namespace smart_objects;
  using namespace am::event_engine;
  using namespace constants;

  const uint32_t app_id = navi_app_->app_id();
  // Precondition
  Event event(FunctionID::BasicCommunication_OnEventChanged);
  SmartObject message;
  message[am::strings::msg_params][am::hmi_notification::is_active] = false;
  message[am::strings::msg_params][am::hmi_notification::event_name] =
      hmi_apis::Common_EventTypes::PHONE_CALL;

  event.set_smart_object(message);
  state_ctrl_->on_event(event);

  am::HmiStatePtr hmi_state = FullAudibleState();

  EXPECT_CALL(*navi_app_ptr_, is_resuming()).WillRepeatedly(Return(true));
  EXPECT_CALL(*navi_app_ptr_, is_navi()).WillRepeatedly(Return(true));
  EXPECT_CALL(*navi_app_ptr_, app_id()).WillRepeatedly(Return(app_id));

  SetBCActivateAppRequestToHMI(hmi_apis::Common_HMILevel::FULL, kCorrID);

  EXPECT_CALL(app_manager_mock_, IsAppTypeExistsInFullOrLimited(_))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*navi_app_ptr_, CurrentHmiState())
      .WillRepeatedly(Return(hmi_state));

  EXPECT_CALL(app_manager_mock_, active_application())
      .WillRepeatedly(Return(am::ApplicationSharedPtr()));

  EXPECT_CALL(*navi_app_ptr_, SetPostponedState(_)).Times(0);

  state_ctrl_->SetRegularState(navi_app_, hmi_state, true);
}

TEST_F(StateControllerImplTest, OnEventOnAppDeactivatedIncorrectHmiLevel) {
  smart_objects::SmartObject msg;
  const uint32_t app_id = simple_app_->app_id();
  msg[am::strings::msg_params][am::strings::app_id] = app_id;
  const hmi_apis::FunctionID::eType event_id =
      hmi_apis::FunctionID::BasicCommunication_OnAppDeactivated;
  am::event_engine::Event event(event_id);
  event.set_smart_object(msg);
  EXPECT_CALL(app_manager_mock_, application(app_id))
      .WillOnce(Return(simple_app_));
  EXPECT_CALL(*simple_app_ptr_, hmi_level())
      .WillOnce(Return(mobile_apis::HMILevel::HMI_BACKGROUND));
  EXPECT_CALL(*simple_app_ptr_, RegularHmiState()).Times(0);
  state_ctrl_->on_event(event);
}

TEST_F(StateControllerImplTest, OnEventOnAppDeactivatedIncorrectApp) {
  smart_objects::SmartObject msg;
  msg[am::strings::msg_params][am::strings::app_id] = 0;
  const hmi_apis::FunctionID::eType event_id =
      hmi_apis::FunctionID::BasicCommunication_OnAppDeactivated;
  am::event_engine::Event event(event_id);
  event.set_smart_object(msg);
  const am::ApplicationSharedPtr incorrect_app;
  EXPECT_CALL(app_manager_mock_, application(_))
      .WillOnce(Return(incorrect_app));
  EXPECT_CALL(*simple_app_ptr_, hmi_level()).Times(0);
  state_ctrl_->on_event(event);
}

TEST_F(StateControllerImplTest, OnEventOnAppDeactivatedAudioApplication) {
  const uint32_t app_id = simple_app_->app_id();
  smart_objects::SmartObject msg;
  msg[am::strings::msg_params][am::strings::app_id] = app_id;
  const hmi_apis::FunctionID::eType event_id =
      hmi_apis::FunctionID::BasicCommunication_OnAppDeactivated;
  am::event_engine::Event event(event_id);
  event.set_smart_object(msg);
  const HmiStatePtr state = LimitedAudibleState();
  // OnAppDeactivated
  EXPECT_CALL(app_manager_mock_, application(app_id))
      .WillOnce(Return(simple_app_));
  EXPECT_CALL(*simple_app_ptr_, app_id()).WillRepeatedly(Return(app_id));
  EXPECT_CALL(*simple_app_ptr_, hmi_level())
      .WillRepeatedly(Return(mobile_apis::HMILevel::HMI_FULL));
  // DeactivateApp
  EXPECT_CALL(*simple_app_ptr_, RegularHmiState()).WillOnce(Return(state));
  EXPECT_CALL(*simple_app_ptr_, IsAudioApplication())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*simple_app_ptr_, CurrentHmiState())
      .WillOnce(Return(BackgroundState()))
      .WillOnce(Return(BackgroundState()));
  state_ctrl_->on_event(event);
}

TEST_F(StateControllerImplTest, OnEventOnAppDeactivatedNotAudioApplication) {
  const uint32_t app_id = simple_app_->app_id();
  smart_objects::SmartObject msg;
  msg[am::strings::msg_params][am::strings::app_id] = app_id;
  const hmi_apis::FunctionID::eType event_id =
      hmi_apis::FunctionID::BasicCommunication_OnAppDeactivated;
  am::event_engine::Event event(event_id);
  event.set_smart_object(msg);
  const HmiStatePtr state = BackgroundState();
  // OnAppDeactivated
  EXPECT_CALL(app_manager_mock_, application(app_id))
      .WillOnce(Return(simple_app_));
  EXPECT_CALL(*simple_app_ptr_, app_id()).WillRepeatedly(Return(app_id));
  EXPECT_CALL(*simple_app_ptr_, hmi_level())
      .WillRepeatedly(Return(mobile_apis::HMILevel::HMI_FULL));
  // DeactivateApp
  EXPECT_CALL(*simple_app_ptr_, RegularHmiState()).WillOnce(Return(state));
  EXPECT_CALL(*simple_app_ptr_, IsAudioApplication())
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*simple_app_ptr_, CurrentHmiState())
      .WillOnce(Return(BackgroundState()))
      .WillOnce(Return(BackgroundState()));
  state_ctrl_->on_event(event);
}

TEST_F(StateControllerImplTest, OnEventOnAppActivatedIncorrectApp) {
  smart_objects::SmartObject msg;
  const uint32_t incorrect_app_ID = 0;
  msg[am::strings::msg_params][am::strings::app_id] = incorrect_app_ID;
  const hmi_apis::FunctionID::eType event_id =
      hmi_apis::FunctionID::BasicCommunication_OnAppActivated;
  am::event_engine::Event event(event_id);
  event.set_smart_object(msg);
  const am::ApplicationSharedPtr incorrect_app;
  EXPECT_CALL(app_manager_mock_, application(_))
      .WillOnce(Return(incorrect_app));
  EXPECT_CALL(*simple_app_ptr_, app_id()).Times(0);
  state_ctrl_->on_event(event);
}

TEST_F(StateControllerImplTest, OnEventOnAppActivated) {
  using namespace constants;

  smart_objects::SmartObject msg;
  for (am::ApplicationSharedPtr app : applications_list_) {
    uint32_t app_id = app->app_id();
    msg[am::strings::msg_params][am::strings::app_id] = app_id;
    const hmi_apis::FunctionID::eType event_id =
        hmi_apis::FunctionID::BasicCommunication_OnAppActivated;
    am::event_engine::Event event(event_id);
    event.set_smart_object(msg);

    EXPECT_CALL(app_manager_mock_, application(app_id)).WillOnce(Return(app));
    // SetRegularState
    EXPECT_CALL(*simple_app_ptr_, app_id()).WillRepeatedly(Return(app_id));
    EXPECT_CALL(*simple_app_ptr_, IsAudioApplication())
        .WillRepeatedly(Return(true));

    smart_objects::SmartObjectSPtr activate_app =
        utils::MakeShared<smart_objects::SmartObject>();
    (*activate_app)[am::strings::params][am::strings::correlation_id] = kCorrID;
    SetBCActivateAppRequestToHMI(hmi_apis::Common_HMILevel::FULL, kCorrID);
    state_ctrl_->on_event(event);
  }
}

TEST_F(StateControllerImplTest, IsStateActive) {
  HmiStatePtr state = FullAudibleState();
  state->set_state_id(HmiState::STATE_ID_CURRENT);
  EXPECT_TRUE(state_ctrl_->IsStateActive(state->state_id()));
  state->set_state_id(HmiState::STATE_ID_REGULAR);
  EXPECT_TRUE(state_ctrl_->IsStateActive(state->state_id()));
  state->set_state_id(HmiState::STATE_ID_TTS_SESSION);
  EXPECT_FALSE(state_ctrl_->IsStateActive(state->state_id()));
}

TEST_F(StateControllerImplTest, IsStateActiveApplyCorrectTempStates) {
  InsertApplication(simple_app_);
  ApplyTempStatesForApplication(
      simple_app_, *simple_app_ptr_, valid_state_ids_);
  for (const am::HmiState::StateID state_id : valid_state_ids_) {
    EXPECT_TRUE(state_ctrl_->IsStateActive(state_id));
  }
}

TEST_F(StateControllerImplTest, IsStateActiveApplyNotCorrectTempStates) {
  smart_objects::SmartObject msg;
  msg[am::strings::msg_params][am::hmi_notification::event_name] =
      hmi_apis::Common_EventTypes::AUDIO_SOURCE;
  const hmi_apis::FunctionID::eType event_id = hmi_apis::FunctionID::VR_Started;
  am::event_engine::Event event(event_id);
  event.set_smart_object(msg);
  state_ctrl_->on_event(event);
  EXPECT_FALSE(state_ctrl_->IsStateActive(HmiState::STATE_ID_AUDIO_SOURCE));
}

TEST_F(StateControllerImplTest, OnApplicationRegisteredDifferentStates) {
  const uint32_t app_id = simple_app_->app_id();
  smart_objects::SmartObject msg;
  msg[am::strings::msg_params][am::strings::app_id] = app_id;
  msg[am::strings::msg_params][am::hmi_notification::is_active] = true;

  const hmi_apis::FunctionID::eType event_id =
      hmi_apis::FunctionID::BasicCommunication_OnEventChanged;
  am::event_engine::Event event(event_id);

  msg[am::strings::msg_params][am::hmi_notification::event_name] =
      hmi_apis::Common_EventTypes::AUDIO_SOURCE;
  event.set_smart_object(msg);
  state_ctrl_->on_event(event);
  msg[am::strings::msg_params][am::hmi_notification::event_name] =
      hmi_apis::Common_EventTypes::PHONE_CALL;
  event.set_smart_object(msg);
  state_ctrl_->on_event(event);
  msg[am::strings::msg_params][am::hmi_notification::event_name] =
      hmi_apis::Common_EventTypes::DEACTIVATE_HMI;
  event.set_smart_object(msg);
  state_ctrl_->on_event(event);
  msg[am::strings::msg_params][am::hmi_notification::event_name] =
      hmi_apis::Common_EventTypes::EMBEDDED_NAVI;
  event.set_smart_object(msg);
  state_ctrl_->on_event(event);

  const am::HmiStatePtr old_state = CreateHmiStateByHmiStateType<am::HmiState>(
      mobile_apis::HMILevel::HMI_FULL,
      mobile_apis::AudioStreamingState::AUDIBLE,
      mobile_apis::VideoStreamingState::NOT_STREAMABLE,
      mobile_apis::SystemContext::SYSCTXT_MAIN,
      simple_app_);

  EXPECT_CALL(*simple_app_ptr_, app_id()).WillRepeatedly(Return(app_id));
  EXPECT_CALL(*simple_app_ptr_, CurrentHmiState())
      .WillRepeatedly(Return(old_state));
  EXPECT_CALL(*simple_app_ptr_, AddHMIState(_)).Times(4);

  const am::HmiStatePtr default_state =
      CreateHmiStateByHmiStateType<am::HmiState>(
          mobile_apis::HMILevel::HMI_BACKGROUND,
          mobile_apis::AudioStreamingState::AUDIBLE,
          mobile_apis::VideoStreamingState::NOT_STREAMABLE,
          mobile_apis::SystemContext::SYSCTXT_MAIN,
          simple_app_);

  EXPECT_CALL(*simple_app_ptr_, RegularHmiState()).WillOnce(Return(old_state));
  EXPECT_CALL(*simple_app_ptr_, CurrentHmiState())
      .WillRepeatedly(Return(default_state));

  EXPECT_CALL(*simple_app_ptr_, ResetDataInNone()).Times(0);
  EXPECT_CALL(app_manager_mock_, SendHMIStatusNotification(_));
  EXPECT_CALL(app_manager_mock_, OnHMILevelChanged(_, _, _));

  state_ctrl_->OnApplicationRegistered(simple_app_,
                                       mobile_apis::HMILevel::HMI_BACKGROUND);
}

TEST_F(StateControllerImplTest, OnApplicationRegisteredEqualStates) {
  const uint32_t app_id = simple_app_->app_id();
  smart_objects::SmartObject msg;
  msg[am::strings::msg_params][am::strings::app_id] = app_id;
  msg[am::strings::msg_params][am::hmi_notification::is_active] = true;

  const hmi_apis::FunctionID::eType event_id =
      hmi_apis::FunctionID::BasicCommunication_OnEventChanged;
  am::event_engine::Event event(event_id);

  msg[am::strings::msg_params][am::hmi_notification::event_name] =
      hmi_apis::Common_EventTypes::AUDIO_SOURCE;
  event.set_smart_object(msg);
  state_ctrl_->on_event(event);
  msg[am::strings::msg_params][am::hmi_notification::event_name] =
      hmi_apis::Common_EventTypes::PHONE_CALL;
  event.set_smart_object(msg);
  state_ctrl_->on_event(event);

  const am::HmiStatePtr old_state = CreateHmiStateByHmiStateType<am::HmiState>(
      mobile_apis::HMILevel::HMI_FULL,
      mobile_apis::AudioStreamingState::AUDIBLE,
      mobile_apis::VideoStreamingState::NOT_STREAMABLE,
      mobile_apis::SystemContext::SYSCTXT_MAIN,
      simple_app_);

  EXPECT_CALL(*simple_app_ptr_, app_id()).WillRepeatedly(Return(app_id));
  EXPECT_CALL(*simple_app_ptr_, CurrentHmiState())
      .WillRepeatedly(Return(old_state));
  EXPECT_CALL(*simple_app_ptr_, AddHMIState(_)).Times(2);

  const am::HmiStatePtr default_state =
      CreateHmiStateByHmiStateType<am::HmiState>(
          mobile_apis::HMILevel::HMI_BACKGROUND,
          mobile_apis::AudioStreamingState::AUDIBLE,
          mobile_apis::VideoStreamingState::NOT_STREAMABLE,
          mobile_apis::SystemContext::SYSCTXT_MAIN,
          simple_app_);
  EXPECT_CALL(*simple_app_ptr_, RegularHmiState())
      .WillOnce(Return(default_state));
  EXPECT_CALL(*simple_app_ptr_, CurrentHmiState())
      .WillRepeatedly(Return(default_state));

  EXPECT_CALL(*simple_app_ptr_, ResetDataInNone()).Times(0);
  EXPECT_CALL(app_manager_mock_, SendHMIStatusNotification(_)).Times(0);
  EXPECT_CALL(app_manager_mock_, OnHMILevelChanged(_, _, _)).Times(0);

  state_ctrl_->OnApplicationRegistered(simple_app_,
                                       mobile_apis::HMILevel::HMI_BACKGROUND);
}

}  // namespace state_controller_test
}  // namespace components
}  // namespace test
