#pragma once

// A server-declared character: an NPC an admin placed in the world, identical for every
// client, persistent across restarts (config/npcs.json). The record names WHO they are
// - any Character.* TweakDB record the game ships - and everything else rides the exact
// replication path player puppets already use: one declaration, many renderers.
struct NpcComponent
{
    std::string Record;
    std::string Name;
};
