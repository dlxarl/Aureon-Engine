#pragma once
#include "CharacterController.h"
#include "GameActors.h"
#include "GameClock.h"
#include "GameProgress.h"
#include "SceneGraph.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class Surface { Grass, Stone, Wood, Sand, Snow };

// Something the player did or had done to them that is worth a sound.
struct SoundCue {
    enum class Kind { Footstep, SwordSwing, SwordHit, ArrowShot, PlayerHurt, Defeat, LevelUp, Pickup, Equip, Mount, Dismount };
    Kind kind;
    glm::vec3 position;
    Surface surface = Surface::Grass;  // under the feet, for a footstep
};

// One frame of player intent. Whoever owns the keyboard fills it in.
struct GameInput {
    float moveForward = 0.0f;   // -1 to 1, relative to the camera
    float moveRight = 0.0f;     // -1 to 1, relative to the camera
    bool jump = false;
    bool sprint = false;
    float turnDegrees = 0.0f;   // how far to swing the camera around the player this frame
    float lookUpDegrees = 0.0f;
    bool attack = false;        // held
    bool dive = false;          // held: go under water, and keep going down
    bool interact = false;      // pressed this frame: talk, take, open, ride...
    bool usePotion = false;     // pressed this frame
    int number = 0;             // a number key pressed this frame: a weapon, or a reply in a conversation
    glm::quat headRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);   // the head's turn from head tracking, added to the view only
};

struct GameStatus {
    bool hasPlayer = false;
    bool firstPerson = false;
    bool won = false;
    bool goalLocked = false;    // the player reached the goal with pickups still missing
    int collected = 0;
    int total = 0;
    int falls = 0;
    float seconds = 0.0f;

    // Worlds with combat
    bool hasCombat = false;
    float health = 100.0f;
    float maxHealth = 100.0f;
    float stamina = 100.0f;
    int weapon = 1;             // 0 nothing in hand, 1 sword, 2 bow
    int banditsLeft = 0;
    int banditsTotal = 0;
    bool mounted = false;
    bool grounded = false;
    bool diving = false;        // swimming under the surface
    bool underwater = false;    // the head is under water
    bool burning = false;       // standing in flames
    float breath = 20.0f;       // seconds of air left
    float maxBreath = 20.0f;
    float moveSpeed = 0.0f;     // how fast the player is going over the ground
    glm::vec3 listenerPosition = glm::vec3(0.0f);
    glm::vec3 listenerForward = glm::vec3(0.0f, 0.0f, -1.0f);

    // Worlds with items, quests or time
    bool hasWorld = false;
    bool swimming = false;
    float hour = 12.0f;
    int day = 1;
    bool night = false;
    std::string prompt;         // what interacting would do right now; empty when nothing
    float rested = 0.0f;        // 1 just after sleeping, fading to 0
};

// The rules of a game built from scene data. Any scene with a Player object is playable; the
// roles objects are given decide the rest (see GameRole):
//
//   pickups, a goal and hazards make a course to finish;
//   ground, weapons, villagers, bandits and animals make a world to fight in;
//   the scene's game definition adds items, conversations, quests and the time of day.
//
// Everything here changes the scene in place, so the caller is expected to restore it
// afterwards, which is what the Editor's Play mode does.
class GameSession {
public:
    // Reads the roles of the scene's objects. Call again after replacing the scene.
    void begin(SceneGraph& scene);

    void update(SceneGraph& scene, const GameInput& input, float deltaTime);

    void setFirstPerson(SceneGraph& scene, bool firstPerson);

    const GameStatus& getStatus() const { return status; }
    const GameProgress& getProgress() const { return progress; }

    // What there is to hear from the last update, which clears them first: the caller reads them
    // after each update and plays them.
    std::vector<SoundCue> cues;
    std::vector<ActorSound> creatureSounds;

    // Whether a person or animal is still alive; anything that never lived counts as alive.
    bool isAlive(ObjectId id) const { return actors.isAlive(id); }

private:
    struct Patrol {
        ObjectId id;
        glm::vec3 home;
    };

    GameStatus status;
    GameProgress progress;
    GameClock clock;
    GameActors actors;
    CharacterController controller;
    CharacterState character;
    CharacterSurroundings surroundings;

    std::vector<Patrol> patrols;
    std::vector<ObjectId> horses;
    std::vector<ObjectId> interactables;
    std::vector<ObjectId> nightGlows;
    std::vector<Shape*> solids;

    // Places with a floor of their own: the footprint of a house, a bridge, a chapel.
    struct Floor {
        glm::mat4 toLocal;
        glm::vec3 low;
        glm::vec3 high;
        Surface surface;
    };
    std::vector<Floor> floors;
    std::vector<glm::vec3> pavedPlaces;   // the heart of a town, cobbled
    std::vector<Shape*> props;
    std::unordered_set<ObjectId> openedChests;
    std::unordered_map<std::string, ObjectId> named;

    glm::vec3 spawnPoint = glm::vec3(0.0f);
    float killHeight = 0.0f;
    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;
    glm::quat headRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);   // from the last input; looks around without turning the player
    float eyeHeight = 0.0f;
    glm::vec3 cameraFocus = glm::vec3(0.0f);

    ObjectId bodyId = InvalidObjectId;
    ObjectId swordId = InvalidObjectId;
    ObjectId swordViewId = InvalidObjectId;   // the sword with a forearm, seen in first person (optional)
    ObjectId bowId = InvalidObjectId;
    ObjectId horseId = InvalidObjectId;   // the one being ridden
    float burnClock = 0.0f;               // seconds in flames since the last hurt
    float drownClock = 0.0f;              // seconds without air since the last hurt
    float waterLevel = 0.0f;              // of the water the head is in
    float stepDistance = 0.0f;            // walked since the last footstep
    float attackCooldown = 0.0f;
    float swing = 0.0f;                   // 1 at the start of a swing or shot, fading to 0
    float stride = 0.0f;
    float staminaRest = 0.0f;             // until stamina starts to come back
    int knownLevel = 1;
    bool glowing = false;

    void animate(SceneGraph& scene, float deltaTime);
    void movePlayer(SceneGraph& scene, const GameInput& input, float deltaTime);
    Surface surfaceUnder(const SceneGraph& scene, glm::vec3 feet) const;
    void cue(SoundCue::Kind kind, glm::vec3 position, Surface surface = Surface::Grass);
    void pushProps(Shape& player, glm::vec3 wishVelocity);
    void updateHorse(SceneGraph& scene);
    void updateInteraction(SceneGraph& scene, const GameInput& input);
    void interact(SceneGraph& scene, Shape& target);
    void updateCombat(SceneGraph& scene, const GameInput& input, float deltaTime);
    void handleEvents();
    void updateQuests(SceneGraph& scene);
    void applyRules(SceneGraph& scene);
    void updateNightGlow(SceneGraph& scene);
    void sleep(SceneGraph& scene);
    void hurtPlayer(SceneGraph& scene, float damage);
    void respawn(SceneGraph& scene);
    void mount(SceneGraph& scene, Shape& horse);
    void dismount(SceneGraph& scene);
    void updateIdleHorses(SceneGraph& scene, float deltaTime);
    void moveCamera(SceneGraph& scene, float deltaTime);
    void placeModels(SceneGraph& scene, float deltaTime);
    glm::vec3 lookDirection() const;
    float cameraReach(const SceneGraph& scene, glm::vec3 from, glm::vec3 direction, float distance) const;
    glm::vec3 playerFeet(const SceneGraph& scene) const;
};
