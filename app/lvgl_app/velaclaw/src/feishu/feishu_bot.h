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

#include "velaclaw_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the Feishu bot module.
 * Loads app_id and app_secret from config_store (or compile-time defaults).
 * Must be called before feishu_bot_start().
 */
int feishu_bot_init(void);

/**
 * Start the background polling task.
 * The task will: obtain access token → create WebSocket connection →
 * receive events → push to message_bus.
 * Auto-reconnects on error / token expiry.
 */
int feishu_bot_start(void);

/**
 * Send a text message to a Feishu chat.
 *
 * @param chat_id  Feishu chat ID (e.g. "oc_xxxxxxxx") or open_id
 * @param text     UTF-8 text to send (split automatically if too long)
 */
int feishu_send_message(const char *chat_id, const char *text);

/**
 * Update the Feishu app credentials at runtime.
 * Saves to config_store; the background task will pick up the change on
 * the next reconnect cycle.
 *
 * @param app_id      App ID of the Feishu Open Platform application
 * @param app_secret  App Secret of the Feishu Open Platform application
 */
int feishu_set_app(const char *app_id, const char *app_secret);

/**
 * Make an authenticated POST request to the Feishu Open API.
 * Automatically refreshes the app_access_token if expired.
 *
 * @param path       API path, e.g. "/open-apis/docx/v1/documents"
 * @param json_body  JSON request body (may be NULL)
 * @param resp_buf   Buffer to receive JSON response body
 * @param resp_cap   Size of resp_buf
 * @return HTTP status code, or negative on error
 */
int feishu_api_post(const char *path, const char *json_body,
                    char *resp_buf, size_t resp_cap);

/**
 * Make an authenticated request (any method) to the Feishu Open API.
 *
 * @param method     HTTP method, e.g. "GET", "POST", "PATCH"
 * @param path       API path
 * @param body       Request body (may be NULL)
 * @param body_len   Length of body
 * @param resp_buf   Buffer to receive JSON response body
 * @param resp_cap   Size of resp_buf
 * @return HTTP status code, or negative on error
 */
int feishu_api_request(const char *method, const char *path,
                       const char *body, size_t body_len,
                       char *resp_buf, size_t resp_cap);

/**
 * Get the current Feishu app_id.
 * Returns pointer to internal static buffer (do not free).
 */
const char *feishu_get_app_id(void);

/**
 * Make an authenticated POST request using user_access_token.
 * Some APIs (e.g. docx:document create) only support user identity.
 * The user_access_token must be set via feishu_set_user_token() or CLI.
 *
 * @param path       API path
 * @param json_body  JSON request body
 * @param resp_buf   Buffer to receive JSON response body
 * @param resp_cap   Size of resp_buf
 * @return HTTP status code, or negative on error
 */
int feishu_api_post_as_user(const char *path, const char *json_body,
                            char *resp_buf, size_t resp_cap);

/**
 * Set the user_access_token for APIs that require user identity.
 * Obtain via OAuth or Feishu developer console.
 */
int feishu_set_user_token(const char *token);

/**
 * Make an authenticated request (any method) using user_access_token.
 * Some APIs (e.g. docx read/write) only support user identity.
 *
 * @param method     HTTP method, e.g. "GET", "POST", "PATCH"
 * @param path       API path
 * @param body       Request body (may be NULL)
 * @param body_len   Length of body
 * @param resp_buf   Buffer to receive JSON response body
 * @param resp_cap   Size of resp_buf
 * @return HTTP status code, or negative on error
 */
int feishu_api_request_as_user(const char *method, const char *path,
                               const char *body, size_t body_len,
                               char *resp_buf, size_t resp_cap);

#ifdef __cplusplus
}
#endif
