/*
 * Copyright (c) 2010 Kevin M. Bowling, <kevin.bowling@kev009.com>, USA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <jansson.h>

#include "craftd-config.h"
#include "util.h"

/* Search path for the main config file, in order */
static const char *config_searchpath[] = {
  "/.craftd/craftd.conf", // $HOME is appended to this in _setdefaults()!
  "/etc/craftd/craftd.conf",
  "/usr/local/etc/craftd/craftd.conf",
  "craftd.conf", // Current working directory (for devs)
  NULL
};

/**
 * This function defines and initializes defaults for the craftd settings
 * structure.
 */
void
craftd_config_setdefaults()
{
  // Add $HOME to the first searchpath entry
  if (getenv("HOME") != NULL)
  {
    char *homeconfig = Malloc(1 + strlen(getenv("HOME")) +
        strlen(config_searchpath[0]));
    homeconfig[0] = '\0';
    strcat(homeconfig, getenv("HOME"));
    strcat(homeconfig, config_searchpath[0]);
    config_searchpath[0] = homeconfig;
  }

  // Game settings
  Config.daemonize = true;
  Config.game_port = 25565;
  Config.max_listenbacklog = 16;
  Config.mcstring_max = 100;
  Config.workpool_size = 2;
  Config.motd_file = "motd.conf";
  
  // httpd settings
  Config.httpd_enabled = true;
  Config.httpd_port = 25566;
  Config.docroot = "htdocs/";
}

/**
 * Read a boolean value from a JSON object and store it as a C99 boolean in the
 * specified storage location.
 */
void
parseJBool(bool *storage, const json_t *obj, const char *key)
{
  json_t *boolobj = json_object_get(obj, key);

  /* Check if the key is defined in the config file (nonfatal) */
  if (!boolobj)
  {
    LOG(LOG_DEBUG, "Config: key \"%s\" is undefined.  Using default.", key);
    return;
  }

  /* Check if the value is a boolean (fatal) */
  if (!json_is_boolean(boolobj))
    ERR("Error processing key: \"%s\".  Value is not boolean!", key);

  /* Set the storage location to the C99 boolean type */
  if (json_is_true(boolobj))
  {
    LOG(LOG_DEBUG, "Got bool value: 'true' for key: \"%s\"", key);
    *storage = true;
  }
  else
  {
    LOG(LOG_DEBUG, "Got bool value: 'false' for key: \"%s\"", key);
    *storage = false;
  }
}

/**
 * Read an integer value from a JSON object and store it in the specified
 * location.  
 * 
 * @remarks 0 is not currently treated as a valid integer
 * 
 * @param storage a location to place the parsed integer value
 * @param obj json object to parse
 * @param key json key to read
 */
void
parseJInt(int *storage, const json_t *obj, const char *key)
{
  int32_t ival;
  json_t *intobj = json_object_get(obj, key);

  /* Check if the key is defined in the config file (nonfatal) */
  if (!intobj)
  {
    LOG(LOG_DEBUG, "Config: key \"%s\" is undefined.  Using default.", key);
    return;
  }
  
  /* Check if the value is a number (fatal) */
  if (!json_is_number(intobj))
    ERR("Error processing key: \"%s\".  Value is not an integer!", key); 
  
  /* Cast and make sure the value is set (fatal) */
  ival = (int32_t)json_integer_value(intobj);
  if (ival == 0)
    ERR("Error processing key: \"%s\"", key);

  LOG(LOG_DEBUG, "Got int value: %d for key: \"%s\"", ival, key);
  
  /* Set the storage location to the new value */
  *storage = ival;
}

/**
 * Read a string value from a JSON object and store it in the specified
 * location.
 * 
 * @param storage a character array pointer to place the parsed string
 * @param obj json object to parse
 * @param key key to read
 */
void
parseJString(char *storage, const json_t *obj, const char *key)
{
  const char *strval;
  json_t *strobj = json_object_get(obj, key);
  
  /* Check if the key is defined in the config file (nonfatal) */
  if (!strobj)
  {
    LOG(LOG_DEBUG, "Config: key \"%s\" is undefined.  Using default.", key);
    return;
  }
  
  /* Check if the value is a string (fatal) */
  if (!json_is_string(strobj))
    ERR("Error processing key: \"%s\".  Value is not a string!", key);
  
  strval = json_string_value(strobj);
  LOG(LOG_DEBUG, "Got string value: \"%s\" for key: \"%s\"", strval, key);
  
  /* Set the storage location to the new value */
  storage = (char *)Malloc(strlen(strval));
  storage = strdup(strval);
}

/**
 * Parse the file name for craftd configuration information.
 *
 * @param file the JSON file name to parse
 */
void
craftd_config_parse(const char *file)
{
  json_t *json, *jsongame, *jsonhttp;
  json_error_t error;

  /* If we didn't get a config file passed as an argument, check search path */
  if (file == NULL)
  {
    int i;
    for (i = 0; config_searchpath[i] != NULL &&
        access(config_searchpath[i], R_OK) != 0; ++i);
  
    file = config_searchpath[i];

    if (file == NULL)
      ERR("Config: craftd.conf not found!");
  }
  /* Check that the file argument exists */
  else
  {
    if (access(file, R_OK) != 0)
      ERR("Config: file %s not readable!");
  }

  LOG(LOG_INFO, "Config: Using file: %s", file);

  json = json_load_file(file, 0, &error);
  if (!json)
  {
    LOG(LOG_ERR, "Config (line: %d, col: %d):\nConfig:%s", error.line,
	error.column, error.text);
    ERR("Cannot parse config file!");
  }
  
  /* Get the general game server configuration */
  jsongame = json_object_get(json, "server");
  if (json_is_object(jsongame))
  {
    parseJBool(&Config.daemonize, jsongame, "daemonize");
    parseJInt(&Config.game_port, jsongame, "game-port");
    parseJInt(&Config.mcstring_max, jsongame, "minecraft-stringmax");
    parseJInt(&Config.workpool_size, jsongame, "worker-pool-size");
    parseJString(Config.motd_file, jsongame, "motd-file");
  }
  else
  {
    LOG(LOG_INFO, "Config: no server section, skipping.");
  }
  
  /* Get the httpd server configuration */
  jsonhttp = json_object_get(json, "httpd");
  if(json_is_object(jsonhttp))
  {
    parseJBool(&Config.httpd_enabled, jsonhttp, "enabled");
    parseJInt(&Config.httpd_port, jsonhttp, "httpd-port");
    parseJString(Config.docroot, jsonhttp, "static-docroot");
  }
  else
  {
    LOG(LOG_INFO, "Config: no httpd section, skipping.");
  }
  
  /* Release the file reader */
  json_decref(json);
}
