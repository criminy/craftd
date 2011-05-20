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

#include <craftd/protocols/survival/World.h>

SVWorld*
SV_CreateWorld (CDServer* server, const char* name)
{
    SVWorld* self = CD_malloc(sizeof(SVWorld));

    assert(name);

    if (pthread_spin_init(&self->lock.time, 0) != 0) {
        CD_abort("pthread spinlock failed to initialize");
    }

    self->server = server;

    C_FOREACH(world, C_PATH(server->config, "server.game.protocol.worlds")) {
         if (CD_CStringIsEqual(name, C_STRING(C_GET(world, "name")))) {
            config_export(world, &self->config.data);
            break;
        }
    }

    self->name      = CD_CreateStringFromCStringCopy(name);
    self->dimension = SVWorldNormal;
    self->time      = 0;

    self->players  = CD_CreateHash();
    self->entities = CD_CreateMap();

    self->chunks = CD_CreateHash();

    self->lastGeneratedEntityId = 0;

    DYNAMIC(self) = CD_CreateDynamic();
    ERROR(self)   = CDNull;

    CD_EventDispatch(server, "World.create", self);

    return self;
}

bool
SV_WorldSave (SVWorld* self)
{
    bool status;

    CD_EventDispatchWithError(status, self->server, "World.save", self);

    return status == CDOk;
}

void
SV_DestroyWorld (SVWorld* self)
{
    assert(self);

    CD_EventDispatch(self->server, "World.destroy", self);

    CD_HASH_FOREACH(self->players, it) {
        SVPlayer* player = (SVPlayer*) CD_HashIteratorValue(it);

        if (player->client->status != CDClientDisconnect) {
            CD_ServerKick(self->server, player->client, NULL);
        }
    }

    CD_DestroyHash(self->players);
    CD_DestroyMap(self->entities);
    CD_DestroyHash(self->chunks);

    CD_DestroyString(self->name);

    CD_DestroyDynamic(DYNAMIC(self));

    pthread_spin_destroy(&self->lock.time);

    config_unexport(&self->config.data);

    CD_free(self);
}

// FIXME: This is just a dummy function
SVEntityId
SV_WorldGenerateEntityId (SVWorld* self)
{
    assert(self);

    if (self->lastGeneratedEntityId != 0) {
        self->lastGeneratedEntityId++;
    } else {
        self->lastGeneratedEntityId = 10;
    }

    return self->lastGeneratedEntityId;
}

bool
SV_WorldAddPlayer (SVWorld* self, SVPlayer* player)
{
    bool ret = true;

    assert(self);
    assert(player);
    assert(player->entity.id == 0);

    // Check to see if the player is already logged-in.
    if (CD_HashHasKey(self->players, CD_StringContent(player->username))) {
        SLOG(self->server, LOG_NOTICE, "%s: nick exists on the server", CD_StringContent(player->username));

        if (self->server->config->cache.game.protocol.standard) {
            // The standard action is to reject the new login attempt 
            // if already logged-in

            ERROR(self) = SVWorldErrUsernameTaken;
            ret = false;
            goto done;

        } else {
            //craftd will let you login multiple times with the same login!

            SLOG(self->server, LOG_INFO, "%s: generating unique username", CD_StringContent(player->username));
            CDString *baseUsername = CD_CloneString(player->username);

            int count = 1;

            // A simple loop adding an increment on the given username
            do {
                CD_DestroyString(player->username);
                player->username = 
                    CD_CreateStringFromFormat("%s^%d",
                        CD_StringContent(baseUsername), count++);

            } while (CD_HashHasKey(self->players, CD_StringContent(player->username)));

            CD_DestroyString(baseUsername);
        }
    }

    player->world = self;
    player->entity.id = SV_WorldGenerateEntityId(self);

    CD_HashPut(self->players, CD_StringContent(player->username),
                (CDPointer) player);
    CD_MapPut(self->entities, player->entity.id, (CDPointer) player);

    done: {
        return ret;
    }
}

void
SV_WorldRemovePlayer (SVWorld* self, SVPlayer* player)
{
    assert(self);
    assert(player);
    assert(player->world == self);

    CD_HashDelete(player->world->players, CD_StringContent(player->username));
    CD_MapDelete(player->world->entities, player->entity.id);
}

void
SV_WorldBroadcastBuffer (SVWorld* self, CDBuffer* buffer)
{
    assert(self);

    CD_HASH_FOREACH(self->players, it) {
        SVPlayer* player = (SVPlayer*) CD_HashIteratorValue(it);

        pthread_rwlock_rdlock(&player->client->lock.status);
        if (player->client->status != CDClientDisconnect) {
            CD_ClientSendBuffer(player->client, buffer);
        }
        pthread_rwlock_unlock(&player->client->lock.status);
    }
}

void
SV_WorldBroadcastPacket (SVWorld* self, SVPacket* packet)
{
    assert(self);

    CDBuffer* buffer = SV_PacketToBuffer(packet);

    SV_WorldBroadcastBuffer(self, buffer);

    CD_DestroyBuffer(buffer);
}

void
SV_WorldBroadcastMessage (SVWorld* self, CDString* message)
{
    assert(self);

    SVPacketChat pkt = {
        .response = {
            .message = message
        }
    };

    SVPacket response = { SVResponse, SVChat, (CDPointer) &pkt };

    SV_WorldBroadcastPacket(self, &response);

    SV_DestroyPacketData(&response);
}

uint16_t
SV_WorldGetTime (SVWorld* self)
{
    uint16_t result;

    assert(self);

    pthread_spin_lock(&self->lock.time);
    result = self->time;
    pthread_spin_unlock(&self->lock.time);

    return result;
}

uint16_t
SV_WorldSetTime (SVWorld* self, uint16_t time)
{
    assert(self);

    pthread_spin_lock(&self->lock.time);
    self->time = time;
    pthread_spin_unlock(&self->lock.time);

    return time;
}

SVChunk*
SV_WorldGetChunk (SVWorld* self, int x, int z)
{
    SVChunk* result;
    CDError  status;
    //TODO: better buffer?
    char buffer[8];
    sprintf(buffer,"%i_%i",x,z);

    // is using the buffer with CD_Hash the quickest/simplest way of storing chunks?
    if(CD_HashHasKey(self->chunks,buffer)) {
       result = (SVChunk*) CD_HashGet(self->chunks,buffer);
       status = CDOk;
    }
    else {
       result = CD_alloc(sizeof(SVChunk));
       CD_EventDispatchWithError(status, self->server, "World.chunk", self, x, z, result);
       if(status == CDOk) {
          CD_HashPut(self->chunks,buffer,(CDPointer) result);
       }
    }

    if (status == CDOk) {
        return result;
    } 
    else {
        CD_free(result);

        errno = CD_ErrorToErrno(status);

        return NULL;
    }
}

void
SV_WorldSetChunk (SVWorld* self, SVChunk* chunk)
{
    CD_EventDispatch(self->server, "World.chunk=", self, chunk->position.x, chunk->position.z, chunk);
}
