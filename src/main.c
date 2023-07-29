#include <stdio.h>
#include <string.h>

#define  _POSIX_C_SOURCE 200809L
#include <stdlib.h>
//#undef _POSIX_C_SOURCE

#include <linux/limits.h>

#include <unistd.h> // sleep

#include <json.h>

#include <concord/discord.h>
#include <concord/log.h>

#define PROJECT_NAME "discord-bot-thingy"

static const char ERROR_PREFIX[] = "Somehow, something went wrong. Please report this to @punaduck: ";

static u64snowflake App_Id = 0;

// ping
const struct discord_create_guild_application_command interaction_ping_def = {
	.name = "ping",
	.description = "Ping command that uses early ACK + artificial delay to mimic a long rendering process"
};
void interaction_ping_cmd (struct discord* client, const struct discord_interaction* event) {
	log_info ("interaction_ping_cmd");
	struct discord_interaction_response paramInitial = {
		.type = DISCORD_INTERACTION_DEFERRED_CHANNEL_MESSAGE_WITH_SOURCE,
		.data = &(struct discord_interaction_callback_data) {
			.content = "Pinging in process..."
		}
	};
	discord_create_interaction_response(client, event->id, event->token, &paramInitial, NULL);

	// test later response
	sleep(5);
	struct discord_edit_original_interaction_response paramEdit = {
		.content = "Pong!"
	};
	discord_edit_original_interaction_response (client, App_Id, event->token, &paramEdit, NULL);
}

// vgmrender
struct discord_application_command_option interaction_vgmrender_def_args[] = {
	{
		.type = DISCORD_APPLICATION_OPTION_ATTACHMENT,
		.name = "vgm",
		.description = "VGM file to render",
		.required = true,
	},
};
const struct discord_create_guild_application_command interaction_vgmrender_def = {
	.name = "vgmrender",
	.description = "Render a VGM",
	.options = &(struct discord_application_command_options) {
		.size = sizeof (interaction_vgmrender_def_args) / sizeof (interaction_vgmrender_def_args[0]),
		.array = interaction_vgmrender_def_args,
	},
};
void interaction_vgmrender_cmd (struct discord* client, const struct discord_interaction* event) {
	log_info ("interaction_vgmrender_cmd");

	if (!event->data || !event->data->options) {
		// TODO refactor into error function
		char buf[1024];
		snprintf (buf, sizeof (buf) / sizeof(buf[0]),
			"User input is missing? Data: %p, Options: %p",
			event->data,
			event->data->options);
		log_error (buf);

		//char buf2[sizeof (buf) / sizeof(buf[0])];
		char buf2[(sizeof (buf) / sizeof(buf[0])) + (sizeof (ERROR_PREFIX) / sizeof(ERROR_PREFIX[0]))];
		snprintf (buf2, sizeof (buf2) / sizeof(buf2[0]),
			"%s%s",
			ERROR_PREFIX,
			buf);
		struct discord_interaction_response paramError = {
			.type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
			.data = &(struct discord_interaction_callback_data) {
				.content = buf2,
			},
		};
		discord_create_interaction_response (client, event->id, event->token, &paramError, NULL);
	}

	struct discord_interaction_response paramInitial = {
		.type = DISCORD_INTERACTION_DEFERRED_CHANNEL_MESSAGE_WITH_SOURCE,
		.data = &(struct discord_interaction_callback_data) {
			.content = "Rendering in process..."
		}
	};
	discord_create_interaction_response (client, event->id, event->token, &paramInitial, NULL);

	char* attachmentId = NULL;
	for (int i = 0; i < event->data->options->size; ++i) {
		char* name = event->data->options->array[i].name;
		json_char* value = event->data->options->array[i].value;
		log_debug ("Argument %s, Type %d, Value %s", name, event->data->options->array[i].type, value);
		if (strcmp (name, "vgm") == 0) {
			attachmentId = value;
		}
	}

	// TODO check for attachmentId still being unset

	struct json_object* snowflakeMapping = json_tokener_parse (event->data->resolved->attachments);
	struct json_object* attachmentDetails;
	if (!json_object_object_get_ex (snowflakeMapping, attachmentId, &attachmentDetails)) {
		// TODO notify of error
		log_error ("%s not found in %s", attachmentId, event->data->resolved->attachments);
		return;
	}

	struct json_object* attachmentUrlObj;
	if (!json_object_object_get_ex (attachmentDetails, "url", &attachmentUrlObj)) {
		// TODO notify of error
		log_error ("%s not found in %s", "url", event->data->resolved->attachments);
		return;
	}

	const char* attachmentUrl = json_object_get_string (attachmentUrlObj);
	log_info ("VGM file: %s", attachmentUrl);

	char workdirTemplate[] = "/tmp/dbt-render.XXXXXX";
	const char* workdir = mkdtemp (workdirTemplate);
	if (workdir == NULL) {
		// TODO notify of error
		log_error ("Failed to create temp dir");
		return;
	}

	// TODO this is awful, write all of this in proper C, I just CBA rn
	char renderCmd[1024];
	snprintf (renderCmd, sizeof (renderCmd) / sizeof (renderCmd[0]),
		"./download_and_render.sh '%s' '%s'",
		attachmentUrl,
		workdir);

	log_info ("Executing: %s", renderCmd);
	int ret = system (renderCmd);

	if (ret != 0) {
		// TODO notify of error
		log_error ("Rendering failed, code %d", ret);
		return;
	}

	char outFile[PATH_MAX];
	snprintf (outFile, PATH_MAX,
		"%s/output.wav",
		workdir);

	struct discord_edit_original_interaction_response paramEdit = {
		.content = "Successfully rendered your request! Uploading result…",
	};
	discord_edit_original_interaction_response (client, App_Id, event->token, &paramEdit, NULL);

	struct discord_create_message paramRender = {
		.attachments = &(struct discord_attachments) {
			.size = 1,
			.array = (struct discord_attachment[]) {
				{ .filename = outFile, },
			},
		},
	};
	discord_create_message (client, event->channel_id, &paramRender, NULL);
}

// registering
struct interaction {
	struct discord_create_guild_application_command description;
	void (*command) (struct discord*, const struct discord_interaction*);
};
const struct interaction interactions[] = {
	{ .description = interaction_ping_def, .command = &interaction_ping_cmd },
	{ .description = interaction_vgmrender_def, .command = &interaction_vgmrender_cmd },
};
void on_ready (struct discord* client, const struct discord_ready* event) {
	for (int i = 0; i < event->guilds->size; ++i) {
		for (int j = 0; j < (sizeof (interactions) / sizeof (struct interaction)); ++j) {
			log_info ("Registering interaction %s on guild %" PRIu64, interactions[j].description.name,
				event->guilds->array[i].id);
			discord_create_guild_application_command (client, event->application->id, event->guilds->array[i].id,
				(struct discord_create_guild_application_command*) &(interactions[j].description), NULL);
		}
	}
	App_Id = event->application->id;
	log_info ("Logged in as %s, ID %" PRIu64 ".", event->user->username, App_Id);
}

void on_interaction (struct discord* client, const struct discord_interaction* event) {
	log_info ("on_interaction");
	if (event->type != DISCORD_INTERACTION_APPLICATION_COMMAND) {
		log_info ("Ignoring non-slash interaction.");
		return;
	}

	for (int i = 0; i < (sizeof (interactions) / sizeof (struct interaction)); ++i) {
		log_info ("Checking interaction %d, name %s", i, interactions[i].description.name);
		if (strcmp (event->data->name, interactions[i].description.name) == 0) {
			log_info ("Matched!");
			(*interactions[i].command) (client, event);
			return;
		}
	}
	log_error ("Unknown command interaction: %s", event->data->name);
}

int main (void) {
	struct discord* client = discord_config_init (PROJECT_NAME ".json");
	discord_set_on_ready (client, &on_ready);
	discord_set_on_interaction_create (client, &on_interaction);

	discord_run (client);

	discord_cleanup (client);
	return 0;
}
