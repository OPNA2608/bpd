#include <stdio.h>
#include <string.h>

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
//#undef _POSIX_C_SOURCE

#include <linux/limits.h>

#include <unistd.h> // sleep
#include <pthread.h>

#include <json.h>

#include <concord/discord.h>
#include <concord/log.h>

#define PROJECT_NAME "bpd"

static const char ERROR_PREFIX[] = "Somehow, something went wrong. Please report this to @punaduck: ";

static u64snowflake App_Id = 0;

struct renderthreadData {
	const char* workDir;
	const char* renderCmd;
	struct discord* client;
	const u64snowflake channelId;
};

struct llFinishedRender {
	char* finishedPath;
	u64snowflake channelId;
	struct llFinishedRender* next;
};
static struct llFinishedRender* finishedRender = NULL;
static pthread_mutex_t llMutex = PTHREAD_MUTEX_INITIALIZER;

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

void* render_thread (void* argsRaw) {
	// CHILD
	log_trace ("In fork");
	struct renderthreadData* args = (struct renderthreadData*) argsRaw;

	size_t len = strlen (args->workDir);
	log_debug ("malloc(%zu)", len);
	char* copyDir = malloc (++len);
	strcpy (copyDir, args->workDir);

	len = strlen (args->renderCmd);
	log_debug ("malloc(%zu)", len);
	char* copyCmd = malloc (++len);
	strcpy (copyCmd, args->renderCmd);

	u64snowflake channelId = args->channelId;

	char* outFile = malloc (PATH_MAX);
	snprintf (outFile, PATH_MAX,
		"%s/output.ogg",
		copyDir);

	log_info ("Executing: %s", copyCmd);
	int ret = system (copyCmd);

	if (ret != 0) {
		// TODO notify of error
		log_error ("Rendering failed, code %d", ret);
		pthread_exit (NULL);
	}

	struct llFinishedRender* newRender = malloc (sizeof (struct llFinishedRender));
	newRender->finishedPath = outFile;
	newRender->channelId = channelId;
	newRender->next = NULL;

	pthread_mutex_lock (&llMutex);
	if (finishedRender == NULL) {
		finishedRender = newRender;
	} else {
		struct llFinishedRender* wander = finishedRender;
		while (wander->next != NULL) wander = wander->next;
		wander->next = newRender;
	}
	pthread_mutex_unlock (&llMutex);

	log_trace ("Render ready to be served! Exiting...");
	pthread_exit (NULL);
}

void render_collector (struct discord* client, struct discord_timer *timer) {
	log_debug ("render_collector");

	pthread_mutex_lock (&llMutex);
	if (finishedRender != NULL) {
		log_debug ("Sending render %s.", finishedRender->finishedPath);
		struct discord_create_message paramRender = {
			.attachments = &(struct discord_attachments) {
				.size = 1,
				.array = (struct discord_attachment[]) {
					{ .filename = finishedRender->finishedPath, },
				},
			},
		};
		CCORDcode sendRet = discord_create_message (client, finishedRender->channelId, &paramRender, NULL);
		if (sendRet != CCORD_OK && sendRet != CCORD_PENDING) {
			log_error ("Failed to send render");
		}
		log_trace ("Render sent!");

		struct llFinishedRender* next = finishedRender->next;
		free (finishedRender);
		finishedRender = next;
	}
	pthread_mutex_unlock (&llMutex);
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
			(void*)event->data,
			(void*)event->data->options);
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

	size_t len = strlen (attachmentUrl);
	log_debug ("malloc(%zu)", len);
	char* threadcopyUrl = malloc (++len);
	strcpy (threadcopyUrl, attachmentUrl);

	len = strlen (workdir);
	log_debug ("malloc(%zu)", len);
	char* threadcopyDir = malloc (++len);
	strcpy (threadcopyDir, workdir);

	len = strlen (renderCmd);
	log_debug ("malloc(%zu)", len);
	char* threadcopyCmd = malloc (++len);
	strcpy (threadcopyCmd, renderCmd);

	struct renderthreadData threadData = {
		.workDir = threadcopyDir,
		.renderCmd = threadcopyCmd,
		.client = client,
		.channelId = event->channel_id,
	};

	pthread_attr_t attr;
	pthread_attr_init (&attr);
	pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
	pthread_t tid;
	if (pthread_create (&tid, &attr, render_thread, (void*)&threadData) != 0) {
		log_error ("Failed to start render thread");
		return;
	}

	sleep (1);

	// PARENT
	log_trace ("In parent");
	struct discord_edit_original_interaction_response paramEdit = {
		.content = "Rendering your request! Result should be sent soon…",
	};
	discord_edit_original_interaction_response (client, App_Id, event->token, &paramEdit, NULL);
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
		for (size_t j = 0; j < (sizeof (interactions) / sizeof (struct interaction)); ++j) {
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

	for (size_t i = 0; i < (sizeof (interactions) / sizeof (struct interaction)); ++i) {
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

	discord_timer_interval (client,
		render_collector,
		NULL,
		NULL,
		0,
		3 * 1000,
		-1);

	discord_run (client);

	discord_cleanup (client);
	return 0;
}
