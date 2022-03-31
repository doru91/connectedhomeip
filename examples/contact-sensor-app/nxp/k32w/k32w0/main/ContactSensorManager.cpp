/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "ContactSensorManager.h"

#include "AppTask.h"
#include "FreeRTOS.h"

#include "app_config.h"

ContactSensorManager ContactSensorManager::sContactSensor;

TimerHandle_t sContactSensorTimer; // FreeRTOS app sw timer.

int ContactSensorManager::Init()
{
    int err = 0;

    // Create FreeRTOS sw timer for Lock timer.

    sContactSensorTimer = xTimerCreate("ContactSensorTmr",        // Just a text name, not used by the RTOS kernel
                              1,                // == default timer period (mS)
                              false,            // no timer reload (==one-shot)
                              (void *) this,    // init timer id = contact sensor obj context
                              TimerEventHandler // timer callback handler
    );

    if (sContactSensorTimer == NULL)
    {
        K32W_LOG("contact sensor timer create failed");
        assert(0);
    }

    mState              = kState_LockingCompleted;


    return err;
}

void ContactSensorManager::SetCallbacks(Callback_fn_initiated aActionInitiated_CB, Callback_fn_completed aActionCompleted_CB)
{
    mActionInitiated_CB = aActionInitiated_CB;
    mActionCompleted_CB = aActionCompleted_CB;
}

bool ContactSensorManager::IsActionInProgress()
{
    return (mState == kState_LockingInitiated || mState == kState_UnlockingInitiated) ? true : false;
}

bool ContactSensorManager::IsUnlocked()
{
    return (mState == kState_UnlockingCompleted) ? true : false;
}

bool ContactSensorManager::InitiateAction(int32_t aActor, Action_t aAction)
{
    bool action_initiated = false;
    State_t new_state;

    // Initiate Lock/Unlock Action only when the previous one is complete.
    if (mState == kState_LockingCompleted && aAction == UNLOCK_ACTION)
    {
        action_initiated = true;

        new_state = kState_UnlockingInitiated;
    }
    else if (mState == kState_UnlockingCompleted && aAction == LOCK_ACTION)
    {
        action_initiated = true;

        new_state = kState_LockingInitiated;
    }

    if (action_initiated)
    {


        StartTimer(ACTUATOR_MOVEMENT_PERIOS_MS);

        // Since the timer started successfully, update the state and trigger callback
        mState = new_state;

        if (mActionInitiated_CB)
        {
            mActionInitiated_CB(aAction, aActor);
        }
    }

    return action_initiated;
}

void ContactSensorManager::StartTimer(uint32_t aTimeoutMs)
{
    if (xTimerIsTimerActive(sContactSensorTimer))
    {
        K32W_LOG("lock timer already started!");
        // appError(err);
        CancelTimer();
    }

    // timer is not active, change its period to required value.
    // This also causes the timer to start.  FreeRTOS- Block for a maximum of
    // 100 ticks if the  change period command cannot immediately be sent to the
    // timer command queue.
    if (xTimerChangePeriod(sContactSensorTimer, aTimeoutMs / portTICK_PERIOD_MS, 100) != pdPASS)
    {
        K32W_LOG("lock timer start() failed");
        // appError(err);
    }
}

void ContactSensorManager::CancelTimer(void)
{
    if (xTimerStop(sContactSensorTimer, 0) == pdFAIL)
    {
        K32W_LOG("contact sensor timer stop() failed");
        // appError(err);
    }
}

void ContactSensorManager::TimerEventHandler(TimerHandle_t xTimer)
{
    // Get lock obj context from timer id.
    ContactSensorManager * contactsensor = static_cast<ContactSensorManager *>(pvTimerGetTimerID(xTimer));

    // The timer event handler will be called in the context of the timer task
    // once sContactSensorTimer expires. Post an event to apptask queue with the actual handler
    // so that the event can be handled in the context of the apptask.
    AppEvent event;
    event.Type               = AppEvent::kEventType_Timer;
    event.TimerEvent.Context = contactsensor;

    event.Handler = ActuatorMovementTimerEventHandler;
    GetAppTask().PostEvent(&event);
}


void ContactSensorManager::ActuatorMovementTimerEventHandler(void * aGenericEvent)
{
    AppEvent * aEvent        = (AppEvent *) aGenericEvent;
    Action_t actionCompleted = INVALID_ACTION;

    ContactSensorManager * contactsensor = static_cast<ContactSensorManager *>(aEvent->TimerEvent.Context);

    if (contactsensor->mState == kState_LockingInitiated)
    {
        contactsensor->mState    = kState_LockingCompleted;
        actionCompleted = LOCK_ACTION;
    }
    else if (contactsensor->mState == kState_UnlockingInitiated)
    {
        contactsensor->mState    = kState_UnlockingCompleted;
        actionCompleted = UNLOCK_ACTION;
    }

    if (actionCompleted != INVALID_ACTION)
    {
        if (contactsensor->mActionCompleted_CB)
        {
            contactsensor->mActionCompleted_CB(actionCompleted);
        }
    }
}
