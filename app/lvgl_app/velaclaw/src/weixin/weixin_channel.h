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

#include <stddef.h>

/**
 * weixin_channel.h — WeChat (Weixin) channel for VelaClaw
 *
 * Direct connection to Tencent iLink Bot API (ilinkai.weixin.qq.com).
 * No intermediate gateway needed — device connects directly via HTTPS.
 *
 * Protocol (iLink Bot API):
 *   All endpoints: POST/GET, JSON body, common headers:
 *     Content-Type: application/json
 *     AuthorizationType: ilink_bot_token
 *     Authorization: Bearer <bot_token>
 *     X-WECHAT-UIN: Base64(decimal_string(random_uint32))
 *
 * Endpoints:
 *   /ilink/bot/get_bot_qrcode   — Get QR code for login
 *   /ilink/bot/get_qrcode_status — Poll QR scan status
 *   /ilink/bot/getupdates       — Long-poll for messages
 *   /ilink/bot/sendmessage      — Send a text message
 *
 * Config keys (config_store):
 *   weixin.host   (default: "ilinkai.weixin.qq.com")
 *   weixin.port   (default: "443")
 *   weixin.token   (bot_token from QR login)
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the WeChat channel.
 * Loads host, port, token from config_store.
 */
int weixin_channel_init(void);

/**
 * Start the background long-polling task.
 * Call only after network is available.
 */
int weixin_channel_start(void);

/**
 * Send a text message to a WeChat user.
 *
 * @param to_user_id     Target user ID (from inbound msg)
 * @param context_token  Context token (from inbound msg, required)
 * @param text           UTF-8 text to send
 */
int weixin_channel_send(const char* to_user_id,
    const char* context_token,
    const char* text);

/** Stop the background polling task. */
void weixin_channel_stop(void);

/**
 * Update the bot token at runtime.
 * @param token  Bot token string (from QR login)
 * @param uin    Ignored (UIN is now per-request random)
 */
int weixin_channel_set_token(const char* token, unsigned int uin);

/**
 * Start QR code login flow.
 * Fetches a QR code URL from iLink Bot API.
 *
 * @param qr_url     Output: URL to render as QR code
 * @param qr_cap     Capacity of qr_url buffer
 * @param qrcode_id  Output: QR code ID for polling status
 * @param qrc_cap    Capacity of qrcode_id buffer
 * @return 0 on success, -1 on error
 */
int weixin_channel_login(char* qr_url, size_t qr_cap,
    char* qrcode_id, size_t qrc_cap);

/**
 * Poll QR code login status.
 *
 * @param qrcode_id  QR code ID from weixin_channel_login()
 * @return  1 = confirmed (token saved),
 *          2 = scanned (waiting confirm),
 *          0 = waiting (not scanned yet),
 *         -1 = error,
 *         -2 = HTTP error,
 *         -3 = QR expired (call login again)
 */
int weixin_channel_poll_login(const char* qrcode_id);

#ifdef __cplusplus
}
#endif
