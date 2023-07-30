#include <stdio.h>
#include <string.h>

#include <unistd.h> // sleep

#include <json.h>

#include <concord/log.h>

#include "common.h"
#include "debugping.h"
#include "vgmrender.h"

const char ERROR_PREFIX[] = "Something went wrong. Please report this to @punaduck: ";

u64snowflake App_Id = 0;

struct llFinishedRender* finishedRenders = NULL;
pthread_mutex_t finishedRendersMutex = PTHREAD_MUTEX_INITIALIZER;

void render_collector (struct discord* client, struct discord_timer *timer) {
	log_trace ("In render_collector");

	pthread_mutex_lock (&finishedRendersMutex);
	if (finishedRenders != NULL) {
		log_info ("Sending render result for %s.", finishedRenders->finishedPath);
		struct discord_create_message paramRender = {
			.content = finishedRenders->message,
			.attachments = &(struct discord_attachments) {
				.size = 1,
				.array = (struct discord_attachment[]) {
					{ .filename = finishedRenders->finishedPath, },
				},
			},
		};

		if (!finishedRenders->success) {
			paramRender.attachments = NULL;
		}

		CCORDcode sendRet = discord_create_message (client, finishedRenders->channelId, &paramRender, NULL);
		if (sendRet != CCORD_OK && sendRet != CCORD_PENDING) {
			log_error ("Failed to send render results");
		}
		log_info ("Render results sent!");

		struct llFinishedRender* next = finishedRenders->next;
		free (finishedRenders->message);
		free (finishedRenders->finishedPath);
		free (finishedRenders);
		finishedRenders = next;
	}
	pthread_mutex_unlock (&finishedRendersMutex);
}

// registering
struct bpd_interaction {
	struct discord_create_guild_application_command description;
	void (*command) (struct discord*, const struct discord_interaction*);
};

const struct discord_create_guild_application_command bpd_interaction_debugping_desc = {
  .name = "debugping",
  .description = "Ping command that uses early ACK + artificial delay to mimic a long rendering process",
};

struct discord_application_command_option interaction_vgmrender_desc_args[] = {
  {
    .type = DISCORD_APPLICATION_OPTION_ATTACHMENT,
    .name = "vgm",
    .description = "VGM file to render",
    .required = true,
  },
};

const struct discord_create_guild_application_command bpd_interaction_vgmrender_desc = {
  .name = "vgmrender",
  .description = "Render a VGM",
  .options = &(struct discord_application_command_options) {
    .size = sizeof (interaction_vgmrender_desc_args) / sizeof (interaction_vgmrender_desc_args[0]),
    .array = interaction_vgmrender_desc_args,
  },
};

const struct bpd_interaction bpd_interactions[] = {
	{ .description = bpd_interaction_debugping_desc, .command = &bpd_interaction_debugping_cmd },
	{ .description = bpd_interaction_vgmrender_desc, .command = &bpd_interaction_vgmrender_cmd },
};
void on_ready (struct discord* client, const struct discord_ready* event) {
	log_trace ("In on_ready");
	App_Id = event->application->id;

	for (int i = 0; i < event->guilds->size; ++i) {
		log_debug ("Checking guild %" PRIu64, event->guilds->array[i].id);

		// check what commands the guild has linked to us
		struct discord_application_commands registeredAppCmds = { 0 };
		struct discord_ret_application_commands ret = {
			.sync = &registeredAppCmds,
		};
		if (discord_get_guild_application_commands(client, App_Id, event->guilds->array[i].id, &ret) != CCORD_OK) continue;
		log_debug ("Found %d interactions linked to us", registeredAppCmds.size);

		// check for commands we no longer have, delete them
		for (int i = 0; i < registeredAppCmds.size; ++i) {
			log_debug ("Interaction %d: %s", i, registeredAppCmds.array[i].name);
			bool stillExists = false;
			for (size_t j = 0; j < (sizeof (bpd_interactions) / sizeof (bpd_interactions[0])); ++j) {
				if (strcmp (registeredAppCmds.array[i].name, bpd_interactions[j].description.name) == 0) {
					stillExists = true;
					break;
				}
			}

			if (!stillExists) {
				log_debug ("Stale interaction %s found, unregistering it", registeredAppCmds.array[i].name);
				discord_delete_guild_application_command (client, App_Id, event->guilds->array[i].id,
					registeredAppCmds.array[i].id, NULL);
			}
		}

		// inform about commands we have
		for (size_t j = 0; j < (sizeof (bpd_interactions) / sizeof (bpd_interactions[0])); ++j) {
			log_debug ("Registering interaction %s", bpd_interactions[j].description.name);
			discord_create_guild_application_command (client, event->application->id, event->guilds->array[i].id,
				(struct discord_create_guild_application_command*) &(bpd_interactions[j].description), NULL);
		}
	}

	log_info ("Logged in as %s, ID %" PRIu64 ".", event->user->username, App_Id);
	log_info ("Interactions:");
	for (size_t i = 0; i < (sizeof (bpd_interactions) / sizeof (bpd_interactions[0])); ++i) {
		log_info ("%zu: %s", i, bpd_interactions[i].description.name);
	}
}

void on_interaction (struct discord* client, const struct discord_interaction* event) {
	log_trace ("In on_interaction");
	if (event->type != DISCORD_INTERACTION_APPLICATION_COMMAND) {
		log_info ("Ignoring attempted non-slash interaction");
		return;
	}

	for (size_t i = 0; i < (sizeof (bpd_interactions) / sizeof (bpd_interactions[0])); ++i) {
		log_debug ("Checking interaction %d, name %s", i, bpd_interactions[i].description.name);
		if (strcmp (event->data->name, bpd_interactions[i].description.name) == 0) {
			log_info ("Matched, executing interaction");
			(*bpd_interactions[i].command) (client, event);
			return;
		}
	}

	log_error ("Unknown command interaction: %s", event->data->name);

	char rejectMsg[1024];
	snprintf (rejectMsg, 1024,
		"I don't know the command '%s'. Maybe it doesn't exist anymore?", event->data->name);

  struct discord_interaction_response paramReject = {
    .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
    .data = &(struct discord_interaction_callback_data) {
      .content = rejectMsg,
    }
  };
  discord_create_interaction_response(client, event->id, event->token, &paramReject, NULL);
}

int main (void) {
	ccord_global_init();

	struct discord* client = discord_config_init (PROJECT_NAME ".json");

	discord_set_on_ready (client, &on_ready);
	discord_set_on_interaction_create (client, &on_interaction);

	discord_timer_interval (client,
		render_collector,
		NULL,
		NULL,
		0,
		3 * 1000,
		-1);

	discord_run (client);

	discord_cleanup (client);
	ccord_global_cleanup();
	return 0;
}
