/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize MQTT channel (read config, no connection yet). */
int mqtt_channel_init(void);

/* Connect to broker and start recv loop thread. Call after network is up. */
int mqtt_channel_start(void);

/* Send a text message to a specific chat_id via MQTT publish. */
int mqtt_channel_send(const char *chat_id, const char *text);

/* Disconnect and stop the recv loop thread. */
void mqtt_channel_stop(void);

#ifdef __cplusplus
}
#endif
