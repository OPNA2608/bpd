#include "debugping.h"
#include "common.h"

#include <unistd.h> // sleep

#include <concord/log.h>

void bpd_interaction_debugping_cmd (struct discord* client, const struct discord_interaction* event) {
  log_info ("interaction_ping_cmd");
  struct discord_interaction_response paramInitial = {
    .type = DISCORD_INTERACTION_DEFERRED_CHANNEL_MESSAGE_WITH_SOURCE,
    .data = &(struct discord_interaction_callback_data) {
      .content = "Pinging in process..."
    }
  };
  discord_create_interaction_response(client, event->id, event->token, &paramInitial, NULL);

  // test delayed response
  sleep(5);

  struct discord_edit_original_interaction_response paramEdit = {
    .content = "Pong!"
  };
  discord_edit_original_interaction_response (client, App_Id, event->token, &paramEdit, NULL);
}
