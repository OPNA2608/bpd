#ifndef DLAR_SCRIPT
	#warning "DLAR_SCRIPT not set, going with './download_and_render.sh'"
	#define DLAR_SCRIPT "./download_and_render.sh"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined (__linux__)
#include <linux/limits.h> // PATH_MAX
#elif defined (__APPLE__)
#include <sys/syslimits.h> // PATH_MAX
#endif

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
	const u64snowflake* channelId;
};

// a thread that runs in the background to render some audio
// receives struct renderthreadData
// on success, puts result into finishedRenders linked-list
void* render_thread (void* argsRaw) {
	struct renderthreadData* args = (struct renderthreadData*) argsRaw;

	char* outFile = malloc (PATH_MAX);
	snprintf (outFile, PATH_MAX,
		"%s/output.ogg",
		args->workDir);

	log_trace ("In render_thread");

	log_info ("Executing: %s", args->renderCmd);
	int ret = system (args->renderCmd);
	int retSan = (ret == -1) ? ret : WEXITSTATUS (ret);

	// we can't inform the user of a failure from here, so register the failed render
	// and let the collector send away the bad news
	bool renderSuccess = (retSan == 0);

	char* message = malloc (DISCORD_MAX_MESSAGE_LEN);
	int formatSuccess;
	if (!renderSuccess) {
		const char* failureMsg = "Rendering `%.*s` failed, code %d!";
		const size_t failureMsgLen = strlen (failureMsg) - 2 - 1 + 1;
		formatSuccess = snprintf (message, DISCORD_MAX_MESSAGE_LEN,
			failureMsg,
			DISCORD_MAX_MESSAGE_LEN - failureMsgLen, args->vgmName,
			retSan);
	} else {
		const char* successMsg = "Here's your render of `%.*s`!";
		const size_t successMsgLen = strlen (successMsg) - 2 + 1;
		formatSuccess = snprintf (message, DISCORD_MAX_MESSAGE_LEN,
			successMsg,
			DISCORD_MAX_MESSAGE_LEN - successMsgLen, args->vgmName);
	}

	if (formatSuccess < 0 || formatSuccess >= DISCORD_MAX_MESSAGE_LEN) {
		log_error ("Formatting response failed: expected 0 <= x < %zu, received response %i",
			DISCORD_MAX_MESSAGE_LEN, formatSuccess);
		// TODO insert owner ping prefix
		snprintf (message, DISCORD_MAX_MESSAGE_LEN,
			"Formatting response failed!");
	}

	struct llFinishedRender* newRender = malloc (sizeof (struct llFinishedRender));
	newRender->success = renderSuccess;
	newRender->message = message;
	newRender->finishedPath = outFile;
	newRender->channelId = args->channelId;
	newRender->next = NULL;

	log_debug ("Registering completed render");
	pthread_mutex_lock (&finishedRendersMutex);
	if (finishedRenders == NULL) {
		finishedRenders = newRender;
	} else {
		volatile struct llFinishedRender* wander = finishedRenders;
		while (wander->next != NULL) wander = wander->next;
		wander->next = newRender;
	}
	pthread_mutex_unlock (&finishedRendersMutex);

	log_info ("Render ready to be served");
	free ((char*) args->workDir);
	free ((char*) args->renderCmd);
	free (argsRaw);
	pthread_exit (NULL);
}

void bpd_interaction_vgmrender_cmd (struct discord* client, const struct discord_interaction* event) {
	log_trace ("In bpd_interaction_vgmrender_cmd");

	log_debug ("Checking for sent user inputs");
	if (!event->data || !event->data->options) {
		// TODO ad-hoc solution, refactor into sharable error function
		char buf[1024];
		snprintf (buf, ARRAY_LENGTH (buf),
			"User input is missing? Data: %p, Options: %p",
			(void*)event->data,
			(void*)event->data->options);
		log_error (buf);

		char buf2[ARRAY_LENGTH (buf)];
		snprintf (buf2, ARRAY_LENGTH (buf2),
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
	snprintf (renderCmd, ARRAY_LENGTH (renderCmd),
		"'%s' '%s' '%s'",
		DLAR_SCRIPT,
		attachmentUrl,
		workdir);

	log_debug ("Preparing rendering thread arguments");

	size_t len = strlen (workdir);
	char* threadcopyDir = malloc (++len);
	strncpy (threadcopyDir, workdir, len);

	len = strlen (renderCmd);
	char* threadcopyCmd = malloc (++len);
	strncpy (threadcopyCmd, renderCmd, len);

	len = strlen (attachmentName);
	char* threadcopyName = malloc (++len);
	strncpy (threadcopyName, attachmentName, len);

	u64snowflake* threadcopyChannel = malloc (sizeof (event->channel_id));
	*threadcopyChannel = event->channel_id;

	struct renderthreadData* threadData = malloc (sizeof (struct renderthreadData));
	threadData->vgmName = threadcopyName;
	threadData->workDir = threadcopyDir;
	threadData->renderCmd = threadcopyCmd;
	threadData->channelId = threadcopyChannel;

	log_info ("Launching rendering thread");
	pthread_attr_t attr;
	pthread_attr_init (&attr);
	pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
	pthread_t tid;
	if (pthread_create (&tid, &attr, render_thread, (void*) threadData) != 0) {
		// TODO notify of error
		log_error ("Failed to start render thread");
		return;
	}

	char acknowledgeMsg[DISCORD_MAX_MESSAGE_LEN];
	const char acknowledgeMsgSkel[] = "Started rendering `%.*s`! This may take some time, please be patient…";
	const size_t acknowledgeMsgSkelLen = strlen (acknowledgeMsgSkel) - 2 + 1;
	int formatSuccess = snprintf (acknowledgeMsg, DISCORD_MAX_MESSAGE_LEN,
		acknowledgeMsgSkel,
		// TODO do checked cast here?
		(int) ((sizeof (acknowledgeMsg) / sizeof (acknowledgeMsg[0])) - acknowledgeMsgSkelLen), attachmentName);

	if (formatSuccess < 0 || formatSuccess >= (int) (sizeof (acknowledgeMsg) / sizeof (acknowledgeMsg[0]))) {
		// TODO notify of error
		log_error ("Failed to format response");
		return;
	}

	struct discord_edit_original_interaction_response paramEdit = {
		.content = acknowledgeMsg,
	};
	discord_edit_original_interaction_response (client, App_Id, event->token, &paramEdit,
		&(struct discord_ret_interaction_response) {
			.sync = DISCORD_SYNC_FLAG,
		});
}
