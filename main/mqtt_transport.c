/*
  mqtt_transport.c - VSCP JSON events over MQTT (lwIP MQTT client)

  Events from the node are published, one VSCP JSON event per message, on
  the publish topic (default vscp/{guid}/{class}/{type}). VSCP JSON events
  published on the subscribe topic (default vscp/{guid}/in) are given to the
  node. The connection is made again when it is lost.

  https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_over_mqtt

  This file is part of the VSCP (https://www.vscp.org)

  The MIT License (MIT)
  Copyright (C) 2026 Ake Hedman, the VSCP project <info@vscp.org>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"

#include "vscp-compiler.h"
#include "vscp-projdefs.h"

#include <vscp-firmware-helper.h>
#include <vscp.h>

#include "blinky.h"

#if BLINKY_ENABLE_MQTT

#define MQTT_JSON_MAX    512 // A VSCP JSON event with 8 data bytes is about 250 bytes
#define MQTT_TOPIC_MAX   128
#define MQTT_RETRY_MS    5000
#define MQTT_KEEPALIVE_S 60

static mqtt_client_t *s_client;
static ip_addr_t s_broker;
static bool s_started;
static bool s_subscribed;
static uint32_t s_last_try;
static char s_guid[48];                   // GUID as a string, FF:FF:...
static char s_sub_topic[MQTT_TOPIC_MAX]; // Subscribe topic
static char s_client_id[40];
static char s_rx[MQTT_JSON_MAX + 1];     // Received message (may come in parts)
static size_t s_rx_len;
static bool s_rx_too_long;

// Make a topic from a template: {guid}, {class} and {type} are replaced
static void
make_topic(char *topic, size_t size, const char *tmpl, const vscp_event_t *pev)
{
  size_t pos = 0;
  topic[0]   = '\0';
  while (*tmpl && (pos < size - 1)) {
    const char *value = NULL;
    char num[8];
    if (0 == strncmp(tmpl, "{guid}", 6)) {
      value = s_guid;
      tmpl += 6;
    }
    else if ((NULL != pev) && (0 == strncmp(tmpl, "{class}", 7))) {
      snprintf(num, sizeof(num), "%u", pev->vscp_class);
      value = num;
      tmpl += 7;
    }
    else if ((NULL != pev) && (0 == strncmp(tmpl, "{type}", 6))) {
      snprintf(num, sizeof(num), "%u", pev->vscp_type);
      value = num;
      tmpl += 6;
    }
    if (NULL != value) {
      pos += snprintf(topic + pos, size - pos, "%s", value);
      if (pos >= size) {
        pos = size - 1;
      }
    }
    else {
      topic[pos++] = *tmpl++;
      topic[pos]   = '\0';
    }
  }
}

// A VSCP JSON event from the broker: give it to the node (handled in vscp_core_work)
static void
handle_message(const char *json)
{
  vscp_event_t *pev = vscp_fwhlp_newEvent();
  if (NULL == pev) {
    return;
  }
  int rv = vscp_fwhlp_parse_json(pev, json);
  if (VSCP_ERROR_SUCCESS != rv) {
    printf("MQTT: not a VSCP JSON event (%d): %.80s\n", rv, json);
    vscp_fwhlp_deleteEvent(&pev);
    return;
  }
  if (VSCP_ERROR_SUCCESS != vscp_core_post(pev)) {
    vscp_fwhlp_deleteEvent(&pev);
  }
}

// lwIP MQTT: a message starts
static void
incoming_publish(void *arg, const char *topic, u32_t tot_len)
{
  s_rx_len      = 0;
  s_rx_too_long = (tot_len > MQTT_JSON_MAX);
}

// lwIP MQTT: message data (possibly in parts)
static void
incoming_data(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
  if (!s_rx_too_long) {
    if (s_rx_len + len > MQTT_JSON_MAX) {
      s_rx_too_long = true;
    }
    else {
      memcpy(s_rx + s_rx_len, data, len);
      s_rx_len += len;
    }
  }
  if (flags & MQTT_DATA_FLAG_LAST) {
    if (s_rx_too_long) {
      printf("MQTT: message too long\n");
      return;
    }
    s_rx[s_rx_len] = '\0';
    handle_message(s_rx);
  }
}

static void
subscribed(void *arg, err_t result)
{
  if (ERR_OK == result) {
    printf("MQTT: subscribed to %s\n", s_sub_topic);
  }
}

// lwIP MQTT: connected or connection lost
static void
connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
  if (MQTT_CONNECT_ACCEPTED == status) {
    printf("MQTT: connected to %s:%d\n", BLINKY_MQTT_BROKER, BLINKY_MQTT_PORT);
    mqtt_set_inpub_callback(client, incoming_publish, incoming_data, NULL);
    s_subscribed = (ERR_OK == mqtt_subscribe(client, s_sub_topic, 0, subscribed, NULL));
  }
  else {
    printf("MQTT: no connection (%d), trying again in %d s\n", status, MQTT_RETRY_MS / 1000);
    s_subscribed = false;
    s_last_try   = millis();
  }
}

static void
connect_broker(void)
{
  struct mqtt_connect_client_info_t ci;
  memset(&ci, 0, sizeof(ci));
  ci.client_id   = s_client_id;
  ci.keep_alive  = MQTT_KEEPALIVE_S;
  ci.client_user = strlen(BLINKY_MQTT_USER) ? BLINKY_MQTT_USER : NULL;
  ci.client_pass = strlen(BLINKY_MQTT_PASSWORD) ? BLINKY_MQTT_PASSWORD : NULL;
  s_last_try     = millis();
  if (ERR_OK != mqtt_client_connect(s_client, &s_broker, BLINKY_MQTT_PORT, connection_cb, NULL, &ci)) {
    printf("MQTT: could not start a connection\n");
  }
}

void
mqtt_transport_send_event(const vscp_event_t *pev)
{
  char topic[MQTT_TOPIC_MAX];
  static char json[MQTT_JSON_MAX];
  if (!s_started || !mqtt_client_is_connected(s_client)) {
    return;
  }
  if (VSCP_ERROR_SUCCESS != vscp_fwhlp_create_json(json, sizeof(json), pev)) {
    printf("MQTT: could not make JSON for event %d/%d\n", pev->vscp_class, pev->vscp_type);
    return;
  }
  // One line: the helper pretty prints (no string in the event has tabs or newlines)
  char *w = json;
  for (char *r = json; *r; r++) {
    if (('\n' != *r) && ('\t' != *r) && ('\r' != *r)) {
      *w++ = *r;
    }
  }
  *w = '\0';
  make_topic(topic, sizeof(topic), BLINKY_MQTT_PUBLISH_TOPIC, pev);
  if (ERR_OK != mqtt_publish(s_client, topic, json, (u16_t) strlen(json), 0, 0, NULL, NULL)) {
    printf("MQTT: could not publish event %d/%d\n", pev->vscp_class, pev->vscp_type);
  }
}

void
mqtt_transport_start(void)
{
  const uint8_t *guid = vscp_core_guid();
  if (!ipaddr_aton(BLINKY_MQTT_BROKER, &s_broker) || (NULL == (s_client = mqtt_client_new()))) {
    printf("MQTT: bad broker address %s\n", BLINKY_MQTT_BROKER);
    return;
  }
  snprintf(s_guid, sizeof(s_guid), "%02X", guid[0]);
  for (int i = 1; i < 16; i++) {
    snprintf(s_guid + strlen(s_guid), sizeof(s_guid) - strlen(s_guid), ":%02X", guid[i]);
  }
  make_topic(s_sub_topic, sizeof(s_sub_topic), BLINKY_MQTT_SUBSCRIBE_TOPIC, NULL);
  snprintf(s_client_id,
           sizeof(s_client_id),
           "vscp-blinky-%02x%02x%02x%02x%02x%02x",
           guid[8],
           guid[9],
           guid[10],
           guid[11],
           guid[12],
           guid[13]);
  s_started = true;
  connect_broker();
}

void
mqtt_transport_work(void)
{
  // Connect again when the connection is lost
  if (s_started && !mqtt_client_is_connected(s_client) && ((millis() - s_last_try) >= MQTT_RETRY_MS)) {
    connect_broker();
  }
}

#else

void
mqtt_transport_send_event(const vscp_event_t *pev)
{
}

void
mqtt_transport_start(void)
{
}

void
mqtt_transport_work(void)
{
}

#endif
