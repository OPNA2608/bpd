#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#undef _POSIX_C_SOURCE

#ifndef DLAR_SCRIPT
	#warning "DLAR_SCRIPT not set, going with './download_and_render.sh'"
	#define DLAR_SCRIPT "./download_and_render.sh"
#endif

#include <string.h>

#include <linux/limits.h> // PATH_MAX
#include <sys/wait.h> // WEXITSTATUS
#include <unistd.h>

#include <json.h>

#include <concord/log.h>

#include "vgmrender.h"
#include "common.h"

// data that is passed to the background render thread
struct renderthreadData {
	const char* vgmName;
	const char* workDir;
	const char* renderCmd;
	const u64snowflake channelId;
};

// a thread that runs in the background to render some audio
// receives struct renderthreadData
// on success, puts result into finishedRenders linked-list
void* render_thread (void* argsRaw) {
	struct renderthreadData* args = (struct renderthreadData*) argsRaw;

	// something seems to be flawed with the way the data is passed to the render thread, seems to be made invalid
	// worked around here by instantly making copies of the values

	size_t len = strlen (args->workDir);
	char* copyDir = malloc (++len);
	strncpy (copyDir, args->workDir, len);

	len = strlen (args->renderCmd);
	char* copyCmd = malloc (++len);
	strncpy (copyCmd, args->renderCmd, len);

	len = strlen (args->vgmName);
	char* vgmName = malloc (++len);
	strncpy (vgmName, args->vgmName, len);

	u64snowflake channelId = args->channelId;

	char* outFile = malloc (PATH_MAX);
	snprintf (outFile, PATH_MAX,
		"%s/output.ogg",
		copyDir);

	log_trace ("In render_thread");

	log_info ("Executing: %s", copyCmd);
	int ret = system (copyCmd);
	int retSan = (ret == -1) ? ret : WEXITSTATUS (ret);

	// we can't inform the user of a failure from here, so register the failed render
	// and let the collector send away the bad news
	bool renderSuccess = (retSan == 0);

	struct llFinishedRender* newRender = malloc (sizeof (struct llFinishedRender));
	newRender->success = renderSuccess;
	newRender->finishedPath = outFile;
	newRender->channelId = channelId;
	newRender->next = NULL;

	char* message = malloc (1024);
	if (!renderSuccess) {
		snprintf (message, 1024,
			"Rendering %s failed, code %d!",
			vgmName,
			retSan);
	} else {
		snprintf (message, 1024,
			"Here's your render of %s!",
			vgmName);
	}
	newRender->message = message;

	log_debug ("Registering completed render");
	pthread_mutex_lock (&finishedRendersMutex);
	if (finishedRenders == NULL) {
		finishedRenders = newRender;
	} else {
		struct llFinishedRender* wander = finishedRenders;
		while (wander->next != NULL) wander = wander->next;
		wander->next = newRender;
	}
	pthread_mutex_unlock (&finishedRendersMutex);

	log_info ("Render ready to be served");
	free (copyDir);
	free (copyCmd);
	pthread_exit (NULL);
}

void bpd_interaction_vgmrender_cmd (struct discord* client, const struct discord_interaction* event) {
	log_trace ("In bpd_interaction_vgmrender_cmd");

	log_debug ("Checking for sent user inputs");
	if (!event->data || !event->data->options) {
		// TODO ad-hoc solution, refactor into sharable error function
		char buf[1024];
		snprintf (buf, sizeof (buf) / sizeof(buf[0]),
			"User input is missing? Data: %p, Options: %p",
			(void*)event->data,
			(void*)event->data->options);
		log_error (buf);

		char buf2[sizeof (buf) / sizeof(buf[0])];
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

	log_debug ("Sending initial rendering request confirmation");
	struct discord_interaction_response paramInitial = {
		.type = DISCORD_INTERACTION_DEFERRED_CHANNEL_MESSAGE_WITH_SOURCE,
		.data = &(struct discord_interaction_callback_data) {
			.content = "Starting rendering process..."
		}
	};
	discord_create_interaction_response (client, event->id, event->token, &paramInitial, NULL);

	log_debug ("Processing user inputs");
	char* attachmentId = NULL;
	for (int i = 0; i < event->data->options->size; ++i) {
		char* name = event->data->options->array[i].name;
		json_char* value = event->data->options->array[i].value;
		log_debug ("Argument %s, Type %d, Value %s", name, event->data->options->array[i].type, value);
		if (strcmp (name, "vgm") == 0) {
			attachmentId = value;
		}
	}

	// TODO check for attachmentId still being unset?

	log_debug ("Extracting VGM name & url");
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

	struct json_object* attachmentNameObj;
	if (!json_object_object_get_ex (attachmentDetails, "filename", &attachmentNameObj)) {
		// TODO notify of error
		log_error ("%s not found in %s", "filename", event->data->resolved->attachments);
		return;
	}

	const char* attachmentUrl = json_object_get_string (attachmentUrlObj);
	const char* attachmentName = json_object_get_string (attachmentNameObj);
	log_info ("Received VGM file %s, URL %s", attachmentName, attachmentUrl);

	char workdirTemplate[] = "/tmp/dbt-render.XXXXXX";
	const char* workdir = mkdtemp (workdirTemplate);
	if (workdir == NULL) {
		// TODO notify of error
		log_error ("Failed to create temp dir");
		return;
	}
	log_info ("Created rendering directory: %s", workdir);

	// TODO this is awful, write all of this in proper C, I just CBA rn
	char renderCmd[1024];
	snprintf (renderCmd, sizeof (renderCmd) / sizeof (renderCmd[0]),
		"'%s' '%s' '%s'",
		DLAR_SCRIPT,
		attachmentUrl,
		workdir);

	log_debug ("Preparing rendering thread arguments");

	size_t len = strlen (workdir);
	char* threadcopyDir = malloc (++len);
	strcpy (threadcopyDir, workdir);

	len = strlen (renderCmd);
	char* threadcopyCmd = malloc (++len);
	strcpy (threadcopyCmd, renderCmd);

	len = strlen (renderCmd);
	char* threadcopyName = malloc (++len);
	strcpy (threadcopyName, attachmentName);

	struct renderthreadData threadData = {
		.vgmName = attachmentName,
		.workDir = threadcopyDir,
		.renderCmd = threadcopyCmd,
		.channelId = event->channel_id,
	};

	log_info ("Launching rendering thread");
	pthread_attr_t attr;
	pthread_attr_init (&attr);
	pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
	pthread_t tid;
	if (pthread_create (&tid, &attr, render_thread, (void*)&threadData) != 0) {
		// TODO notify of error
		log_error ("Failed to start render thread");
		return;
	}

	struct discord_edit_original_interaction_response paramEdit = {
		.content = "Started rendering your request! This may take some time, please be patient…",
	};
	discord_edit_original_interaction_response (client, App_Id, event->token, &paramEdit, NULL);

	// something seems to be flawed with the way the data is passed to the render thread, seems to be made invalid
	// worked around here by waiting abit before returning
	sleep (1);
}
