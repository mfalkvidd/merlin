#include <glib.h>
#include <stdlib.h>

#include "cukesocket.h"
#include "jsonsocket.h"
#include "jsonx.h"

typedef struct CukeScenarioHandler_ {
	CukeStepEnvironment *stepenv;
	gpointer userdata;
} CukeScenarioHandler;

typedef struct CukeConnection_ {
	CukeSocket *cs;
	GTree *cur_stepenvs;
} CukeConnection;

struct CukeSocket_ {
	JSONSocket *js;
	GPtrArray *stepenvs;
};

static gpointer cukesock_cb_new(GSocket *conn, gpointer userdata);
static gboolean cukesock_cb_data(GSocket *conn, JsonNode *node,
		gpointer userdata);
static void cukesock_cb_close(gpointer userdata);

static CukeScenarioHandler *cukesock_scenario_handler_new(
		CukeStepEnvironment *stepenv);
static void cukesock_scenario_handler_destroy(gpointer userdata);

CukeSocket *cukesock_new(const gchar *bind_addr, const gint bind_port) {
	CukeSocket *cs = g_malloc(sizeof(CukeSocket));
	cs->js = jsonsocket_new(bind_addr, bind_port, cukesock_cb_new,
			cukesock_cb_data, cukesock_cb_close, cs);

	cs->stepenvs = g_ptr_array_new();
	return cs;
}
void cukesock_destroy(CukeSocket *cs) {
	if (cs == NULL)
		return;
	g_ptr_array_unref(cs->stepenvs);
	jsonsocket_destroy(cs->js);
	g_free(cs);
}

void cukesock_register_stepenv(CukeSocket *cs, CukeStepEnvironment *stepenv) {
	g_return_if_fail(cs != NULL);
	g_return_if_fail(stepenv != NULL);
	g_ptr_array_add(cs->stepenvs, stepenv);
}

static gpointer cukesock_cb_new(GSocket *conn, gpointer userdata) {
	g_message("New connection");
	CukeSocket *cs = (CukeSocket *) userdata;
	CukeConnection *cconn = g_malloc(sizeof(CukeConnection));
	cconn->cs = cs;
	cconn->cur_stepenvs = NULL;
	return cconn;
}
static gboolean cukesock_cb_data(GSocket *conn, JsonNode *node,
		gpointer userdata) {
	CukeConnection *cconn = (CukeConnection*) userdata;
	CukeSocket *cs = cconn->cs;
	const char *cmd = NULL;
	JsonNode *response = json_mkarray();

	if (!jsonx_locate(node, 'a', 0, 's', &cmd)) {
		json_append_element(response, json_mkstring("fail"));
		goto do_send;
	}

	/*
	 * Find a step, given a name to match, which will be returned as an id tag,
	 * generated out of which module, and which step id, that matches
	 */
	if (0 == strcmp(cmd, "step_matches")) {
		int cur_stepenv;
		int cur_stepdef;
		CukeStepEnvironment *curenv;
		CukeStepDefinition *curdef;

		const char *name_to_match;
		if (!jsonx_locate(node, 'a', 1, 'o', "name_to_match", 's',
				&name_to_match)) {
			json_append_element(response, json_mkstring("fail"));
			goto do_send;
		}

		/* Traverse step environments */
		for (cur_stepenv = 0; cur_stepenv < cs->stepenvs->len; cur_stepenv++) {
			curenv = cs->stepenvs->pdata[cur_stepenv];

			/* Traverse step definitions within the step enviornments */
			for (cur_stepdef = 0; cur_stepdef < curenv->num_defs;
					cur_stepdef++) {
				curdef = &curenv->definitions[cur_stepdef];

				/* If we match, send a matching id tag */
				if (0 == strcmp(name_to_match, curdef->match)) {
					json_append_element(response, json_mkstring("success"));
					json_append_element(response,
							jsonx_packarray(
									jsonx_packobject("id",
											jsonx_packarray(
													json_mkstring(curenv->tag),
													json_mknumber(cur_stepdef),
													NULL), "args",
											json_mkarray(), "source",
											json_mkstring(curenv->tag),
											NULL, NULL),
									NULL));
					goto do_send;
				}
			}
		}

		/* No step found, should be success, but without id tag */
		json_append_element(response, json_mkstring("success"));
		json_append_element(response, json_mkarray());
		goto do_send;
	}

	/*
	 * Invoke a step, identified by and id-tag, located above
	 */
	if (0 == strcmp(cmd, "invoke")) {
		const char *tag = NULL;
		long idx = 0;

		CukeScenarioHandler *scenhandler = NULL;
		CukeStepDefinition *stepdef;

		JsonNode *stepargs = NULL;

		if (!jsonx_locate(node, 'a', 1, 'o', "id", 'a', 0, 's', &tag)
				|| !jsonx_locate(node, 'a', 1, 'o', "id", 'a', 1, 'l', &idx)) {
			json_append_element(response, json_mkstring("fail"));
			json_append_element(response,
					jsonx_packobject("message",
							json_mkstring("Malformed id tag"), "exception",
							json_mkstring("CukeMerlin-internal"),
							NULL, NULL));
			goto do_send;
		}

		/* We need a current active scenario */
		g_return_val_if_fail(cconn->cur_stepenvs != NULL, FALSE);

		/* Locate the step environment and step, given tag and idx */
		scenhandler = g_tree_lookup(cconn->cur_stepenvs, tag);
		if (scenhandler == NULL || idx < 0
				|| idx >= scenhandler->stepenv->num_defs) {
			json_append_element(response, json_mkstring("fail"));
			json_append_element(response,
					jsonx_packobject("message",
							json_mkstring("Unknown step definition id"),
							"exception", json_mkstring("CukeMerlin-internal"),
							NULL, NULL));
			goto do_send;
		}

		/* Locate correct step definition within the stepenv */
		stepdef = &scenhandler->stepenv->definitions[idx]; /* idx bounds verified above */

		/* Locate step arguments, if availble, otherwise NULL */
		if (!jsonx_locate(node, 'a', 1, 'o', "args", 'j', &stepargs)) {
			stepargs = NULL;
		}

		/* Call the handler */
		if ((*stepdef->handler)(scenhandler->userdata, stepargs)) {
			/* Step succeeds */
			json_append_element(response, json_mkstring("success"));
			goto do_send;
		} else {
			/* Step fails */
			json_append_element(response, json_mkstring("fail"));
			json_append_element(response,
					jsonx_packobject("message", json_mkstring("Step error"),
							"exception", json_mkstring("CukeMerlin-execution"),
							NULL, NULL));
			goto do_send;
		}
	}

	/*
	 * Start a new scenario, and load all step environments for that scenario
	 */
	if (0 == strcmp(cmd, "begin_scenario")) {
		JsonNode *taglist = NULL;
		JsonNode *curtag = NULL;
		cconn->cur_stepenvs = g_tree_new_full((GCompareDataFunc) g_strcmp0,
		NULL, g_free, cukesock_scenario_handler_destroy);
		g_message("New scenario");

		if (jsonx_locate(node, 'a', 1, 'o', "tags", 'j', &taglist)
				&& taglist->tag == JSON_ARRAY) {
			/*
			 * We got a tag list, load scenarios. No tags is valid for us to,
			 * we just don't load the environments then. Step might come from
			 * another cucumber step_definition source.
			 */
			json_foreach(curtag, taglist)
			{
				int cur_stepenv;
				CukeStepEnvironment *curenv;
				if (curtag->tag != JSON_STRING)
					continue;
				for (cur_stepenv = 0; cur_stepenv < cs->stepenvs->len;
						cur_stepenv++) {
					curenv = cs->stepenvs->pdata[cur_stepenv];
					if (0 == strcmp(curenv->tag, curtag->string_)) {
						g_tree_insert(cconn->cur_stepenvs,
								g_strdup(curenv->tag),
								cukesock_scenario_handler_new(curenv));
					}
				}
			}
		}

		json_append_element(response, json_mkstring("success"));
		goto do_send;
	}

	/*
	 * End the scenario. Deallocating the stepenv tree should finish up all
	 * step environments
	 */
	if (0 == strcmp(cmd, "end_scenario")) {
		g_message("Finish up scenario");
		g_return_val_if_fail(cconn->cur_stepenvs != NULL, FALSE);

		g_tree_destroy(cconn->cur_stepenvs);
		cconn->cur_stepenvs = NULL;

		json_append_element(response, json_mkstring("success"));
		goto do_send;
	}

	/*
	 * If unknown, we fall back to just simply "success", which means we just
	 * havn't implemented a handler on the method, so we accept the default
	 * cucumber behavior
	 */
	json_append_element(response, json_mkstring("success"));

	do_send: /**/
	jsonsocket_send(conn, response);
	json_delete(response);

	return TRUE;
}
static void cukesock_cb_close(gpointer userdata) {
	CukeConnection *cconn = (CukeConnection*) userdata;
	if (cconn->cur_stepenvs) {
		g_tree_destroy(cconn->cur_stepenvs);
	}
	g_free(cconn);
	g_message("Disconnected");
}

static CukeScenarioHandler *cukesock_scenario_handler_new(
		CukeStepEnvironment *stepenv) {
	CukeScenarioHandler *scenario = g_malloc(sizeof(CukeScenarioHandler));
	scenario->stepenv = stepenv;
	if (scenario->stepenv->begin_scenario) {
		scenario->userdata = (*scenario->stepenv->begin_scenario)();
	} else {
		scenario->userdata = NULL;
	}
	return scenario;
}

static void cukesock_scenario_handler_destroy(gpointer userdata) {
	CukeScenarioHandler *scenario = (CukeScenarioHandler *) userdata;
	if (!scenario)
		return;
	if (scenario->stepenv->end_scenario) {
		(*scenario->stepenv->end_scenario)(scenario->userdata);
	}
	g_free(scenario);
}