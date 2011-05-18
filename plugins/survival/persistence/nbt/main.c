/*
 * Copyright (c) 2010-2011 Kevin M. Bowling, <kevin.bowling@kev009.com>, USA
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

#include <sys/stat.h>
#include <fcntl.h>

#include <craftd/Server.h>
#include <craftd/Plugin.h>

#include <craftd/protocols/survival.h>

#define MAP_WIDTH 500
#define MAP_HEIGHT 500

uint8_t** chunk_generated;
SVChunk** chunks;

#include "include/nbt.h"
#include "include/itoa.h"


static struct {
    const char* path;

    int base;
} _config;

#include "helpers.c"

static
bool
cdnbt_WorldCreate (CDServer* server, SVWorld* world)
{
    int       error = CDNull;
    CDString* path  = CD_CreateStringFromFormat("%s/%s/level.dat", _config.path, CD_StringContent(world->name));
    nbt_node* root  = nbt_parse_path(CD_StringContent(path));

    if (!root || errno != NBT_OK || !cdnbt_ValidLevel(root)) {
        goto error;
    }

    world->spawnPosition = (SVBlockPosition) {
        .x = nbt_find_by_path(root, ".Data.SpawnX")->payload.tag_int,
        .y = nbt_find_by_path(root, ".Data.SpawnY")->payload.tag_int,
        .z = nbt_find_by_path(root, ".Data.SpawnZ")->payload.tag_int
    };

    WDEBUG(world, "spawn position: (%d, %d, %d)",
        world->spawnPosition.x,
        world->spawnPosition.y,
        world->spawnPosition.z);

    SV_WorldSetTime(world, nbt_find_by_path(root, ".Data.Time")->payload.tag_long);

    done: {
        if (root) {
            nbt_free(root);
        }

        return true;
    }

    error: {
        int     old = errno;
        CDError status;

        if (root) {
            nbt_free(root);
        }

        CD_EventDispatchWithError(status, server, "Mapgen.level", world, NULL);

        if (status != CDOk) {
            WERR(world, "Couldn't load world base data: %s", nbt_error_to_string(old));
        }
        else {
            WDEBUG(world, "spawn position: (%d, %d, %d)",
                world->spawnPosition.x,
                world->spawnPosition.y,
                world->spawnPosition.z);
        }

        return true;
    }
}

static
bool
cdnbt_WorldGetChunk (CDServer* server, SVWorld* world, int x, int z, SVChunk* chunk, CDError* error)
{
    CDString* chunkPath = cdnbt_ChunkPath(world, x, z);

    WDEBUG(world, "loading chunk %s", CD_StringContent(chunkPath));



    nbt_node* root = nbt_parse_path(CD_StringContent(chunkPath));

    if (!root || errno != NBT_OK || !cdnbt_ValidChunk(root)) {

 	if(chunk_generated[x+100][z+100] == 1) {
            goto load_chunk;
	}	

        if (cdnbt_GenerateChunk(world, x, z, chunk, NULL) == CDOk) {
//            WERR(world, "generated chunk: %d,%d", x, z);
	    chunk_generated[x+100][z+100] = 1;
            goto done;
        }
        else {
            WERR(world, "bad chunk file '%s'", CD_StringContent(chunkPath));
            goto error;
        }
    }

    load_chunk: {
	SVChunk* src = &chunks[x+100][z+100];
	memcpy(chunk->heightMap,src->heightMap,256);
	memcpy(chunk->blocks,src->blocks,32768);
	memcpy(chunk->data,src->data,16384);
	memcpy(chunk->blockLight,src->blockLight,16384);
	memcpy(chunk->skyLight,src->skyLight,16384);
    }
/*
    nbt_node* node;

    node = nbt_find_by_path(root, ".Level.HeightMap");
    memcpy(chunk->heightMap, node->payload.tag_byte_array.data, 256);

    node = nbt_find_by_path(root, ".Level.Blocks");
    memcpy(chunk->blocks, node->payload.tag_byte_array.data, 32768);

    node = nbt_find_by_path(root, ".Level.Data");
    memcpy(chunk->data, node->payload.tag_byte_array.data, 16384);

    node = nbt_find_by_path(root, ".Level.BlockLight");
    memcpy(chunk->blockLight, node->payload.tag_byte_array.data, 16384);

    node = nbt_find_by_path(root, ".Level.SkyLight");
    memcpy(chunk->skyLight, node->payload.tag_byte_array.data, 16384);
*/

    done: {
        if (root) {
            nbt_free(root);
        }

        CD_DestroyString(chunkPath);

        return true;
    }

    error: {
        if (root) {
            nbt_free(root);
        }

        CD_DestroyString(chunkPath);

        *error = 1;

        return true;
    }
}

static
bool
cdnbt_WorldSetChunk (CDServer* server, SVWorld* world, int x, int z, SVChunk* chunk)
{
    SVChunk* dst = &chunks[x+100][z+100];
    memcpy(dst->heightMap,chunk->heightMap,256);
    memcpy(dst->blocks,chunk->blocks,32768);
    memcpy(dst->data,chunk->data,16384);
    memcpy(dst->blockLight,chunk->blockLight,16384);
    memcpy(dst->skyLight,chunk->skyLight,16384);

    return true;
}

static
bool
cdnbt_WorldSave (CDServer* server, SVWorld* world)
{


    return true;
}

static
bool
cdnbt_WorldDestroy (CDServer* server, SVWorld* world)
{
    return true;
}

static
bool
cdnbt_PersistenceObserve (CDServer* server, const char* type, const char* name)
{
    CDHash* observing = (CDHash*) CD_DynamicGet(server, "Persistence.observing");

    if (observing == NULL) {
        CD_DynamicPut(server, "Persistence.observing", (CDPointer) (observing = CD_CreateHash()));
    }

    CDList* names = (CDList*) CD_HashGet(observing, type);

    if (names == NULL) {
        CD_HashPut(observing, type, (CDPointer) (names = CD_CreateList()));
    }

    CD_ListPushIf(names, (CDPointer) name, (CDListCompareCallback)cdnbt_NameNotObserved);

    return true;
}

static
bool
cdnbt_PersistenceWatch (CDServer* server, CDPointer object, const char* type)
{
    CDHash* watching = (CDHash*) CD_DynamicGet(server, "Persistence.watching");

    if (watching == NULL) {
        CD_DynamicPut(server, "Persistence.watching", (CDPointer) (watching = CD_CreateHash()));
    }

    CDList* objects = (CDList*) CD_HashGet(watching, type);

    if (objects == NULL) {
        CD_HashPut(watching, type, (CDPointer) (objects = CD_CreateList()));
    }

    CD_ListPushIf(objects, object, (CDListCompareCallback) cdnbt_ObjectNotWatched);

    return true;
}

static
bool
cdnbt_ServerDestroy (CDServer* server)
{
    CDHash* observing = (CDHash*) CD_DynamicGet(server, "Persistence.observing");
    CDHash* watching  = (CDHash*) CD_DynamicGet(server, "Persistence.watching");

    if (observing) {
        CD_HASH_FOREACH(observing, it) {
            CDList* names = (CDList*) CD_HashIteratorValue(it);

            CD_LIST_FOREACH(names, it) {
                CD_free((char*) CD_ListIteratorValue(it));
            }

            CD_DestroyList(names);
        }

        CD_DestroyHash(observing);
    }

    if (watching) {
        CD_HASH_FOREACH(watching, it) {
            CDList* objects = (CDList*) CD_HashIteratorValue(it);

            CD_DestroyList(objects);
        }

        CD_DestroyHash(watching);
    }

    return true;
}

extern
bool
CD_PluginInitialize (CDPlugin* self)
{
    self->description = CD_CreateStringFromCString("cNBT Persistence");

    DO { // Initialize configuration stuff
        _config.path = "/usr/share/craftd/worlds";
        _config.base = 36;

        C_SAVE(C_PATH(self->config, "path"), C_STRING, _config.path);
        C_SAVE(C_PATH(self->config, "base"), C_INT, _config.base);
	
	


    }

	chunks = (SVChunk**)malloc(MAP_WIDTH*sizeof(SVChunk*));
	for(int i = 0; i < MAP_WIDTH; i++) {
		chunks[i] = (SVChunk*)malloc(MAP_HEIGHT*sizeof(SVChunk));
	}


	chunk_generated = (uint8_t**)CD_malloc(MAP_WIDTH*sizeof(uint8_t*));
	for(int i = 0; i < MAP_WIDTH; i++) {
		chunk_generated[i] = (uint8_t*)CD_malloc(MAP_HEIGHT*sizeof(uint8_t));
	}


    CD_EventRegister(self->server, "World.create",  cdnbt_WorldCreate);
    CD_EventRegister(self->server, "World.chunk",   cdnbt_WorldGetChunk);
    CD_EventRegister(self->server, "World.chunk=",  cdnbt_WorldSetChunk);
    CD_EventRegister(self->server, "World.save",    cdnbt_WorldSave);
    CD_EventRegister(self->server, "World.destroy", cdnbt_WorldDestroy);

    CD_EventRegister(self->server, "Server.destroy", cdnbt_ServerDestroy);

    CD_EventProvides(self->server, "Persistence.initialized", CD_CreateEventParameters("CDPlugin", NULL));
    CD_EventProvides(self->server, "Persistence.observe",     CD_CreateEventParameters("char*", "char*", NULL));
    CD_EventProvides(self->server, "Persistence.watch",       CD_CreateEventParameters("char*", "CDPointer", NULL));

    CD_EventDispatch(self->server, "Persistence.initialized", self);

    return true;
}

extern
bool
CD_PluginFinalize (CDPlugin* self)
{
    CD_EventUnregister(self->server, "World.create",  cdnbt_WorldCreate);
    CD_EventUnregister(self->server, "World.chunk",   cdnbt_WorldGetChunk);
    CD_EventUnregister(self->server, "World.chunk=",   cdnbt_WorldSetChunk);
    CD_EventUnregister(self->server, "World.save",    cdnbt_WorldSave);
    CD_EventUnregister(self->server, "World.destroy", cdnbt_WorldDestroy);

    return true;
}
