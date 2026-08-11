#include "hal/serial/hal_serial_session.h"

#include "hal/security/jh_secure_random.h"
#include "hal/serial/hal_serial.h"
#include "hal/serial/hal_serial_frame.h"

#include <stdio.h>
#include <string.h>

namespace {

void emit_hello(hal_serial_session_t *session) {
  char response[192] = {0};
  snprintf(response, sizeof(response),
           "OK HELLO module=%s proto=%u session=%lu fw=%s build=%s uid=%s",
           session->module_tag, (unsigned)HAL_SERIAL_SESSION_PROTOCOL_VERSION,
           (unsigned long)session->session_id, session->fw_version,
           session->build_id, session->uid_hex);
  hal_serial_session_println(session, response);
  jh_secure_zeroize(response, sizeof(response));
}

#ifdef HAL_ENABLE_CRYPTO

void reset_auth(hal_serial_session_t *session) {
  session->authenticated = false;
  session->challenge_pending = false;
  jh_secure_zeroize(session->challenge, sizeof(session->challenge));
}

bool hex_decode(const char *hex, uint8_t *out, size_t out_len) {
  if (hex == nullptr || out == nullptr) {
    return false;
  }
  for (size_t index = 0u; index < out_len; ++index) {
    uint8_t value = 0u;
    for (uint8_t digit = 0u; digit < 2u; ++digit) {
      const char character = hex[index * 2u + digit];
      uint8_t nibble = 0u;
      if (character >= '0' && character <= '9') {
        nibble = static_cast<uint8_t>(character - '0');
      } else if (character >= 'A' && character <= 'F') {
        nibble = static_cast<uint8_t>(10 + (character - 'A'));
      } else if (character >= 'a' && character <= 'f') {
        nibble = static_cast<uint8_t>(10 + (character - 'a'));
      } else {
        return false;
      }
      value = static_cast<uint8_t>((value << 4u) | nibble);
    }
    out[index] = value;
  }
  return true;
}

bool generate_challenge(hal_serial_session_t *session) {
  jh_secure_zeroize(session->challenge, sizeof(session->challenge));
  if (jh_secure_random_bytes(session->challenge, sizeof(session->challenge)) !=
      HAL_OK) {
    jh_secure_zeroize(session->challenge, sizeof(session->challenge));
    return false;
  }
  ++session->auth_counter;
  return true;
}

void handle_auth_begin(hal_serial_session_t *session) {
  if (!session->active) {
    hal_serial_session_println(
        session,
        HAL_SERIAL_SESSION_VOCAB(session, reply_not_ready_hello_required));
    return;
  }

  /* Starting a new handshake invalidates any authority from the old one. */
  reset_auth(session);
  if (!generate_challenge(session)) {
    hal_serial_session_println(
        session, HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_entropy));
    return;
  }
  session->challenge_pending = true;

  char hex[HAL_SC_AUTH_CHALLENGE_HEX_BUF_SIZE] = {0};
  static const char k_hex[] = "0123456789abcdef";
  for (size_t index = 0u; index < HAL_SC_AUTH_CHALLENGE_BYTES; ++index) {
    hex[index * 2u] = k_hex[(session->challenge[index] >> 4u) & 0x0Fu];
    hex[index * 2u + 1u] = k_hex[session->challenge[index] & 0x0Fu];
  }

  const char *format =
      HAL_SERIAL_SESSION_VOCAB(session, reply_auth_challenge_fmt);
  if (format != nullptr) {
    char response[64] = {0};
    snprintf(response, sizeof(response), format, hex);
    hal_serial_session_println(session, response);
    jh_secure_zeroize(response, sizeof(response));
  }
  jh_secure_zeroize(hex, sizeof(hex));
}

void handle_auth_prove(hal_serial_session_t *session, const char *args) {
  if (!session->active) {
    hal_serial_session_println(
        session,
        HAL_SERIAL_SESSION_VOCAB(session, reply_not_ready_hello_required));
    return;
  }
  if (!session->challenge_pending) {
    hal_serial_session_println(
        session,
        HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_no_challenge));
    return;
  }

  uint8_t provided[HAL_SC_AUTH_RESPONSE_BYTES] = {0u};
  uint8_t key[HAL_SC_AUTH_KEY_BYTES] = {0u};
  uint8_t expected[HAL_SC_AUTH_RESPONSE_BYTES] = {0u};
  const char *reply = nullptr;
  bool authenticated = false;

  while (args != nullptr && *args == ' ') {
    ++args;
  }

  size_t hex_len = 0u;
  if (args != nullptr) {
    while (args[hex_len] != '\0' && args[hex_len] != ' ') {
      ++hex_len;
    }
  }

  if (hex_len != static_cast<size_t>(HAL_SC_AUTH_RESPONSE_BYTES) * 2u) {
    reply = HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_bad_length);
  } else if (!hex_decode(args, provided, sizeof(provided))) {
    reply = HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_bad_hex);
  } else if (!hal_sc_auth_derive_device_key(session->uid_bytes,
                                            HAL_DEVICE_UID_BYTES, key)) {
    reply = HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_key_derivation);
  } else if (!hal_sc_auth_compute_response(key, session->challenge,
                                           sizeof(session->challenge),
                                           session->session_id, expected)) {
    reply = HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_mac_compute);
  } else if (!hal_sc_auth_macs_equal(provided, expected, sizeof(expected))) {
    reply = HAL_SERIAL_SESSION_VOCAB(session, reply_auth_failed_bad_mac);
  } else {
    authenticated = true;
    reply = HAL_SERIAL_SESSION_VOCAB(session, reply_auth_ok);
  }

  /* A proof attempt always consumes its challenge. */
  session->challenge_pending = false;
  session->authenticated = authenticated;
  if (!authenticated) {
    ++session->auth_failures;
  }
  jh_secure_zeroize(session->challenge, sizeof(session->challenge));
  jh_secure_zeroize(provided, sizeof(provided));
  jh_secure_zeroize(key, sizeof(key));
  jh_secure_zeroize(expected, sizeof(expected));
  hal_serial_session_println(session, reply);
}

void handle_reboot_bootloader(hal_serial_session_t *session) {
  if (!session->authenticated) {
    hal_serial_session_println(
        session, HAL_SERIAL_SESSION_VOCAB(session, reply_not_authorized));
    return;
  }
  hal_serial_session_println(
      session, HAL_SERIAL_SESSION_VOCAB(session, reply_reboot_ok));
  hal_delay_ms(50u);
  hal_enter_bootloader();
}

#endif /* HAL_ENABLE_CRYPTO */

void handle_bye(hal_serial_session_t *session) {
  hal_serial_session_println(session,
                             HAL_SERIAL_SESSION_VOCAB(session, reply_bye_ok));
  session->active = false;
  session->last_activity_ms = hal_millis();
#ifdef HAL_ENABLE_CRYPTO
  reset_auth(session);
#endif
}

void dispatch_inner(hal_serial_session_t *session, const char *inner) {
  if (strcmp(inner, "HELLO") == 0) {
    session->active = true;
    ++session->hello_counter;
    session->last_activity_ms = hal_millis();
    session->session_id = (session->hello_counter << 20u) ^
                          (session->last_activity_ms & 0x000FFFFFu);
#ifdef HAL_ENABLE_CRYPTO
    reset_auth(session);
#endif
    emit_hello(session);
    return;
  }

  const char *cmd_bye = HAL_SERIAL_SESSION_VOCAB(session, cmd_bye);
  if (cmd_bye != nullptr && strcmp(inner, cmd_bye) == 0) {
    handle_bye(session);
    return;
  }

#ifdef HAL_ENABLE_CRYPTO
  const char *cmd_auth_begin =
      HAL_SERIAL_SESSION_VOCAB(session, cmd_auth_begin);
  if (cmd_auth_begin != nullptr && strcmp(inner, cmd_auth_begin) == 0) {
    handle_auth_begin(session);
    return;
  }

  const char *cmd_auth_prove =
      HAL_SERIAL_SESSION_VOCAB(session, cmd_auth_prove);
  if (cmd_auth_prove != nullptr) {
    const size_t command_length = strlen(cmd_auth_prove);
    if (strncmp(inner, cmd_auth_prove, command_length) == 0 &&
        (inner[command_length] == ' ' || inner[command_length] == '\0')) {
      handle_auth_prove(session, inner + command_length);
      return;
    }
  }

  const char *cmd_reboot =
      HAL_SERIAL_SESSION_VOCAB(session, cmd_reboot_bootloader);
  if (cmd_reboot != nullptr && strcmp(inner, cmd_reboot) == 0) {
    handle_reboot_bootloader(session);
    return;
  }
#endif

  if (session->unknown_handler != nullptr) {
    session->unknown_handler(inner, session->unknown_user);
  } else {
    hal_serial_session_println(
        session, HAL_SERIAL_SESSION_VOCAB(session, reply_unknown_cmd));
  }
}

} // namespace

void hal_serial_session_init_with_vocabulary(
    hal_serial_session_t *session, const char *module_tag,
    const char *fw_version, const char *build_id,
    const hal_serial_session_vocabulary_t *vocab) {
  if (session == nullptr) {
    return;
  }
  jh_secure_zeroize(session, sizeof(*session));
  session->module_tag =
      module_tag != nullptr ? module_tag : HAL_SERIAL_SESSION_UNKNOWN;
  session->fw_version = fw_version != nullptr && fw_version[0] != '\0'
                            ? fw_version
                            : HAL_SERIAL_SESSION_UNKNOWN;
  session->build_id = build_id != nullptr && build_id[0] != '\0'
                          ? build_id
                          : HAL_SERIAL_SESSION_UNKNOWN;
#ifdef HAL_ENABLE_CRYPTO
  hal_get_device_uid(session->uid_bytes);
#endif
  if (!hal_get_device_uid_hex(session->uid_hex, sizeof(session->uid_hex))) {
    session->uid_hex[0] = '\0';
  }
  session->vocab = vocab;
}

void hal_serial_session_init(hal_serial_session_t *session,
                             const char *module_tag, const char *fw_version,
                             const char *build_id) {
  hal_serial_session_init_with_vocabulary(session, module_tag, fw_version,
                                          build_id, nullptr);
}

void hal_serial_session_set_unknown_handler(hal_serial_session_t *session,
                                            hal_serial_session_unknown_cb_t cb,
                                            void *user) {
  if (session == nullptr) {
    return;
  }
  session->unknown_handler = cb;
  session->unknown_user = user;
}

bool hal_serial_session_is_active(const hal_serial_session_t *session) {
  return session != nullptr ? session->active : false;
}

uint32_t hal_serial_session_id(const hal_serial_session_t *session) {
  return session != nullptr ? session->session_id : 0u;
}

bool hal_serial_session_is_authenticated(const hal_serial_session_t *session) {
#ifdef HAL_ENABLE_CRYPTO
  return session != nullptr ? session->authenticated : false;
#else
  (void)session;
  return false;
#endif
}

bool hal_serial_session_should_mute_debug_for_line(const char *line) {
  return line != nullptr && strncmp(line, HAL_SERIAL_FRAME_PREFIX,
                                    HAL_SERIAL_FRAME_PREFIX_LEN) == 0;
}

void hal_serial_session_println(hal_serial_session_t *session,
                                const char *payload) {
  if (session == nullptr || payload == nullptr || !session->in_request) {
    return;
  }
  char framed[HAL_SERIAL_FRAME_LINE_MAX] = {0};
  if (hal_serial_frame_encode(session->request_seq, payload, framed,
                              sizeof(framed), nullptr)) {
    hal_serial_println(framed);
  }
  jh_secure_zeroize(framed, sizeof(framed));
}

void hal_serial_session_poll(hal_serial_session_t *session) {
  if (session == nullptr || session->module_tag == nullptr) {
    return;
  }

  while (hal_serial_available() > 0) {
    const int raw = hal_serial_read();
    if (raw < 0) {
      break;
    }

    const char character = static_cast<char>(raw);
    if (character != '\r' && character != '\n') {
      if (session->line_len < HAL_SERIAL_SESSION_MAX_LINE) {
        session->line[session->line_len++] = character;
      }
      continue;
    }
    if (session->line_len == 0u) {
      continue;
    }

    session->line[session->line_len] = '\0';
    const uint8_t line_length = session->line_len;
    session->line_len = 0u;

    if (line_length < HAL_SERIAL_FRAME_PREFIX_LEN ||
        strncmp(session->line, HAL_SERIAL_FRAME_PREFIX,
                HAL_SERIAL_FRAME_PREFIX_LEN) != 0) {
      jh_secure_zeroize(session->line, sizeof(session->line));
      continue;
    }

    const bool mute_debug =
        hal_serial_session_should_mute_debug_for_line(session->line);
    const bool debug_was_muted = mute_debug ? hal_debug_is_muted() : false;
    if (mute_debug && !debug_was_muted) {
      hal_debug_set_muted(true);
    }

    uint16_t sequence = 0u;
    char inner[HAL_SERIAL_SESSION_MAX_LINE + 1u] = {0};
    if (hal_serial_frame_decode(session->line, &sequence, inner,
                                sizeof(inner))) {
      session->in_request = true;
      session->request_seq = sequence;
      dispatch_inner(session, inner);
      session->in_request = false;
    }

    if (mute_debug && !debug_was_muted) {
      hal_debug_set_muted(false);
    }
    jh_secure_zeroize(inner, sizeof(inner));
    jh_secure_zeroize(session->line, sizeof(session->line));
  }
}
