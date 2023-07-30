#include <unistd.h> // sleep

#include <concord/log.h>

#include "debugping.h"
#include "common.h"

void bpd_interaction_debugping_cmd (struct discord* client, const struct discord_interaction* event) {
  log_trace ("In bpd_interaction_debugping_cmd");

	log_debug ("Sending initial deferred message");
  struct discord_interaction_response paramInitial = {
    .type = DISCORD_INTERACTION_DEFERRED_CHANNEL_MESSAGE_WITH_SOURCE,
    .data = &(struct discord_interaction_callback_data) {
      .content = "Pinging in process..."
    }
  };
  discord_create_interaction_response(client, event->id, event->token, &paramInitial, NULL);

  // test delayed response
	log_debug ("Simulating processing delay");
  sleep(5);

	log_debug ("Sending followup edit");
  struct discord_edit_original_interaction_response paramEdit = {
    .content = "Pong!"
  };
  discord_edit_original_interaction_response (client, App_Id, event->token, &paramEdit, NULL);
}
