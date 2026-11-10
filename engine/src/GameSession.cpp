#include "GameSession.h"
#include "Terrain.h"
#include "MeshShape.h"
#include "Player.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace {
    const float RUN_SPEED = 6.5f;
    const float SPRINT_FACTOR = 1.55f;
    const float FALL_LIMIT = 14.0f;       // how far below the start counts as lost
    const float PICKUP_SPIN = 140.0f;     // degrees per second
    const float CAMERA_DISTANCE = 11.0f;
    const float CAMERA_HEIGHT = 6.0f;
    const float CAMERA_LOOK_AHEAD = 1.0f;
    const float CAMERA_FOLLOW = 7.0f;     // higher catches up with the player sooner
    const float TWO_PI = 6.2831853f;
    const float MAX_LOOK_PITCH = 85.0f;
    const float EYE_FOLLOW = 20.0f;       // absorbs the small hops of ground contact

    const float MOUNTED_SPEED = 13.0f;
    const float MOUNT_RANGE = 3.2f;
    const float MOUNTED_EYE_RISE = 1.1f;

    const float SWORD_RANGE = 2.6f;
    const float SWORD_DAMAGE = 40.0f;
    const float SWORD_SECONDS = 0.5f;
    const float BOW_SECONDS = 0.8f;
    const float ARROW_SPEED = 42.0f;
    const float ARROW_DAMAGE = 55.0f;

    const float STAMINA_MAX = 100.0f;
    const float STAMINA_DRAIN = 22.0f;    // per second of sprinting
    const float STAMINA_REGEN = 16.0f;
    const float STAMINA_PAUSE = 0.8f;     // after sprinting, before it comes back
    const float SAFE_FALL_SPEED = 14.0f;
    const float FALL_DAMAGE_PER_SPEED = 6.0f;

    const float INTERACT_RANGE = 2.8f;
    const float REACH_RANGE = 7.0f;
    const float PUSH_TRANSFER = 0.9f;
    const float REST_SAFE_RANGE = 14.0f;
    const float WAKE_HOUR = 7.0f;
    const float DEFEAT_GOLD_LOSS = 0.1f;

    const float BREATH_SECONDS = 20.0f;
    const float BREATH_RECOVERY = 5.0f;   // times as fast as it runs out
    const float DROWN_DAMAGE = 12.0f;     // each second without air
    const float CAMERA_WATER_GAP = 0.35f;

    const float STEP_LENGTH = 2.0f;       // ground covered between footsteps
    const float SPRINT_STEP_LENGTH = 2.7f;
    const float SNOW_HEIGHT = 38.0f;      // terrain above this is snow
    const float PAVED_RADIUS = 20.0f;     // around a town's well or market stalls
    const float SHORE_MARGIN = 0.9f;      // within this of the water's surface it is sand

    bool containsAny(std::string text, std::initializer_list<const char*> words) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return std::any_of(words.begin(), words.end(), [&](const char* word) { return text.find(word) != std::string::npos; });
    }

    // Where the camera sits for a given yaw, which is 0 looking towards -Z.
    glm::vec3 cameraForward(float yawDegrees) {
        const float yaw = glm::radians(yawDegrees);
        return glm::vec3(std::sin(yaw), 0.0f, -std::cos(yaw));
    }

    // "potion:2, gold:25" or just "potion".
    std::vector<ItemStack> parseStacks(const std::string& text) {
        std::vector<ItemStack> stacks;
        std::stringstream list(text);
        std::string entry;
        while (std::getline(list, entry, ',')) {
            entry.erase(0, entry.find_first_not_of(' '));
            entry.erase(entry.find_last_not_of(' ') + 1);
            if (entry.empty()) continue;

            ItemStack stack;
            const size_t colon = entry.find(':');
            stack.item = entry.substr(0, colon);
            if (colon != std::string::npos) stack.count = std::max(1, std::atoi(entry.c_str() + colon + 1));
            stacks.push_back(stack);
        }
        return stacks;
    }

    const char* roleKey(GameRole role) {
        switch (role) {
            case GameRole::Bandit:    return "bandit";
            case GameRole::Wolf:      return "wolf";
            case GameRole::Deer:      return "deer";
            case GameRole::Livestock: return "livestock";
            case GameRole::Boar:      return "boar";
            case GameRole::Bear:      return "bear";
            case GameRole::Rabbit:    return "rabbit";
            case GameRole::Bird:      return "bird";
            case GameRole::Dragon:    return "dragon";
            default:                  return "villager";
        }
    }
}

glm::vec3 GameSession::playerFeet(const SceneGraph& scene) const {
    const Player* player = scene.getPlayer();
    return player->position - glm::vec3(0.0f, player->scale.y * 0.5f, 0.0f);
}

void GameSession::begin(SceneGraph& scene) {
    status = GameStatus();
    cues.clear();
    creatureSounds.clear();
    stepDistance = 0.0f;
    patrols.clear();
    horses.clear();
    interactables.clear();
    nightGlows.clear();
    solids.clear();
    floors.clear();
    pavedPlaces.clear();
    props.clear();
    openedChests.clear();
    named.clear();
    character = CharacterState();
    surroundings = CharacterSurroundings();
    scene.ground = HeightField();
    scene.colliders.clear();
    bodyId = swordId = swordViewId = bowId = horseId = InvalidObjectId;
    attackCooldown = swing = stride = staminaRest = 0.0f;
    glowing = false;

    // Footsteps are told apart by what the player stands on, which comes from names.
    for (const auto& shape : scene.getShapes()) {
        const std::string name = shape->name;
        if (containsAny(name, {"windows"})) continue;
        const glm::vec3 place = shape->getWorldPosition();
        if (containsAny(name, {"market stall", "town well", "street lamp"})) pavedPlaces.push_back(place);

        // Stonebridge is a village, so the chapels are told from the bridges first.
        Surface surface;
        if (containsAny(name, {"chapel", "church", "abbey", "crypt"})) {
            surface = Surface::Stone;
        } else if (containsAny(name, {"bridge", "deck", "dock", "hut", "house", "cottage", "barn", "farmhouse", "cabin", "tavern", "mill"})) {
            surface = Surface::Wood;
        } else if (containsAny(name, {"keep", "tower", "gate", "ruin"})) {
            surface = Surface::Stone;
        } else {
            continue;
        }
        const auto* mesh = dynamic_cast<const MeshShape*>(shape.get());
        if (!mesh || !mesh->getMesh()) continue;
        floors.push_back({glm::inverse(shape->getWorldMatrix()), mesh->getMesh()->getBoundsMin(), mesh->getMesh()->getBoundsMax(), surface});
    }

    progress.begin(scene.game);
    clock.begin(scene.game.world);
    knownLevel = progress.level;
    status.hasWorld = !scene.game.isEmpty();

    Player* player = scene.getPlayer();
    status.hasPlayer = player != nullptr && player->gameControls;
    if (!status.hasPlayer) return;

    spawnPoint = player->position;
    killHeight = spawnPoint.y - FALL_LIMIT;
    cameraYaw = 0.0f;
    cameraFocus = spawnPoint;
    player->runSpeed = RUN_SPEED;
    player->velocity = glm::vec3(0.0f);

    std::unordered_set<std::string> stationary;
    for (const CharacterDefinition& who : scene.game.characters) {
        if (who.stays) stationary.insert(who.name);
    }

    // Water that is an object of its own.
    for (const auto& water : scene.getWaters()) {
        const glm::vec3 at = water->getWorldPosition();
        const glm::vec2 half = 0.5f * water->size * glm::vec2(water->getWorldScale().x, water->getWorldScale().z);
        surroundings.waters.push_back({at.y, glm::vec4(at.x - half.x, at.z - half.y, at.x + half.x, at.z + half.y), water->round});
    }

    // A sculpted terrain makes its mesh when first drawn; the ground is built from it, so it must exist now.
    for (const auto& shape : scene.getShapes()) {
        if (auto* terrain = dynamic_cast<TerrainObject*>(shape.get())) {
            if (terrain->needsRebuild() || !terrain->getMesh()) terrain->rebuild();
        }
    }

    // Terrain may come in pieces; all of them make one ground.
    std::vector<glm::vec3> groundTriangles;

    for (const auto& shape : scene.getShapes()) {
        named.emplace(shape->name, shape->id);

        if (shape->gameRole == GameRole::None) {
            if (shape->hasCollision && shape.get() != player) {
                solids.push_back(shape.get());
                if (!shape->isStatic) props.push_back(shape.get());
            }
            continue;
        }

        // These are touched, not stood on, so they must not stop the player.
        shape->hasCollision = false;

        if (shape->name == "Sword View") swordViewId = shape->id;
        switch (shape->gameRole) {
            case GameRole::Pickup:
                status.total++;
                break;
            case GameRole::Hazard:
                if (glm::length(shape->patrolOffset) > 0.0f) {
                    patrols.push_back({shape->id, shape->position});
                }
                break;
            case GameRole::Ground:
                if (auto* mesh = dynamic_cast<MeshShape*>(shape.get()); mesh && mesh->getMesh()) {
                    const glm::mat4 world = mesh->getWorldMatrix();
                    for (const glm::vec3& vertex : mesh->getMesh()->getTriangles()) {
                        groundTriangles.push_back(glm::vec3(world * glm::vec4(vertex, 1.0f)));
                    }
                }
                break;
            case GameRole::Water:
                if (auto* mesh = dynamic_cast<MeshShape*>(shape.get()); mesh && mesh->getMesh()) {
                    const glm::vec3 low = mesh->getWorldPosition() + mesh->getMesh()->getBoundsMin() * mesh->getWorldScale();
                    const glm::vec3 high = mesh->getWorldPosition() + mesh->getMesh()->getBoundsMax() * mesh->getWorldScale();
                    surroundings.waters.push_back({mesh->getWorldPosition().y, glm::vec4(low.x, low.z, high.x, high.z)});
                }
                break;
            case GameRole::Horse:
                horses.push_back(shape->id);
                interactables.push_back(shape->id);
                break;
            case GameRole::Villager:
                if (scene.game.findCharacter(shape->name)) interactables.push_back(shape->id);
                break;
            case GameRole::Item:
            case GameRole::Chest:
            case GameRole::Bed:
            case GameRole::Sign:
                interactables.push_back(shape->id);
                break;
            case GameRole::NightGlow:
                nightGlows.push_back(shape->id);
                break;
            case GameRole::PlayerBody:
                bodyId = shape->id;
                break;
            case GameRole::Sword:
                swordId = shape->id;
                break;
            case GameRole::Bow:
                bowId = shape->id;
                break;
            default:
                break;
        }
    }

    if (!groundTriangles.empty()) scene.ground.build(groundTriangles, glm::mat4(1.0f));
    scene.colliders.build(solids);
    surroundings.colliders = &scene.colliders;
    scene.waterAreas = surroundings.waters;
    surroundings.ground = scene.ground.isValid() ? &scene.ground : nullptr;

    actors.begin(scene, stationary);
    status.hasCombat = swordId != InvalidObjectId || bowId != InvalidObjectId;
    status.maxHealth = progress.maxHealth();
    status.health = status.maxHealth;
    status.stamina = STAMINA_MAX;
    status.weapon = swordId != InvalidObjectId ? 1 : 2;
    status.banditsTotal = actors.banditsTotal();
    status.banditsLeft = status.banditsTotal;

    // With a separate body, the Player object is only an invisible collider.
    if (bodyId != InvalidObjectId) {
        player->castShadow = false;
    }

    clock.apply(scene);
    status.hour = clock.getHour();
    status.night = clock.isNight();
    updateNightGlow(scene);

    CameraObject* camera = scene.getMainCamera();
    setFirstPerson(scene, camera && camera->firstPerson);
}

void GameSession::setFirstPerson(SceneGraph& scene, bool firstPerson) {
    status.firstPerson = firstPerson;
    cameraPitch = 0.0f;

    if (Player* player = scene.getPlayer()) {
        // The camera sits inside the body, which still throws a shadow to land by.
        player->shadowOnly = firstPerson || bodyId != InvalidObjectId;
        if (Shape* body = scene.findAs<Shape>(bodyId)) {
            // The whole body, arms and legs too, is only a shadow when seen from inside it.
            std::vector<SceneObject*> parts{body};
            for (size_t i = 0; i < parts.size(); i++) {
                if (auto* part = dynamic_cast<Shape*>(parts[i])) part->shadowOnly = firstPerson;
                for (SceneObject* child : parts[i]->getChildren()) parts.push_back(child);
            }
        }
        eyeHeight = player->getCameraPosition().y;
    }
    moveCamera(scene, 0.0f);
}

void GameSession::update(SceneGraph& scene, const GameInput& input, float deltaTime) {
    cues.clear();
    creatureSounds.clear();
    if (!status.hasPlayer || !scene.getPlayer()) return;

    progress.update(deltaTime);
    status.rested = std::max(0.0f, status.rested - deltaTime * 0.5f);

    if (clock.isRunning()) {
        clock.update(deltaTime);
        clock.apply(scene);
        updateNightGlow(scene);
    }
    status.hour = clock.getHour();
    status.day = clock.getDay();
    status.night = clock.isNight();

    animate(scene, deltaTime);

    if (!status.won) {
        status.seconds += deltaTime;

        // A conversation keeps the player in place; only the view moves.
        GameInput action = input;
        if (progress.getDialogue().open) {
            action = GameInput();
        }

        movePlayer(scene, action, deltaTime);
        if (status.mounted) {
            updateHorse(scene);
        } else {
            updateIdleHorses(scene, deltaTime);
        }
        updateInteraction(scene, input);
        updateCombat(scene, action, deltaTime);
        updateQuests(scene);
        applyRules(scene);
    }

    cameraYaw += input.turnDegrees;
    cameraPitch = std::clamp(cameraPitch + input.lookUpDegrees, -MAX_LOOK_PITCH, MAX_LOOK_PITCH);
    headRotation = input.headRotation;
    moveCamera(scene, deltaTime);
    placeModels(scene, deltaTime);

    // Heard from where the player is, looking the way the camera does: with a tracked head, that
    // includes the head's turn.
    status.listenerPosition = scene.getPlayer()->getCameraPosition();
    status.listenerForward = lookDirection();
    if (const CameraObject* camera = scene.getMainCamera()) {
        status.listenerForward = -glm::normalize(glm::vec3(camera->getWorldMatrix()[2]));
    }
}

void GameSession::animate(SceneGraph& scene, float deltaTime) {
    for (const auto& shape : scene.getShapes()) {
        if (shape->gameRole == GameRole::Pickup && shape->visible) {
            shape->rotate(PICKUP_SPIN * deltaTime, glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }

    for (const Patrol& patrol : patrols) {
        if (Shape* shape = scene.findAs<Shape>(patrol.id)) {
            const float phase = status.seconds / std::max(shape->patrolSeconds, 0.1f);
            shape->setPosition(patrol.home + shape->patrolOffset * std::sin(phase * TWO_PI));
        }
    }
}

void GameSession::movePlayer(SceneGraph& scene, const GameInput& input, float deltaTime) {
    Player* player = scene.getPlayer();

    const glm::vec3 forward = cameraForward(cameraYaw);
    const glm::vec3 right(-forward.z, 0.0f, forward.x);

    glm::vec3 direction = forward * input.moveForward + right * input.moveRight;
    if (glm::length(direction) > 1.0f) {
        direction = glm::normalize(direction);
    }
    const bool moving = glm::length(direction) > 0.1f;

    float speed = status.mounted ? MOUNTED_SPEED : RUN_SPEED;
    const bool sprinting = input.sprint && moving && !status.mounted && !character.swimming && status.stamina > 1.0f;
    if (sprinting) {
        speed *= SPRINT_FACTOR;
        status.stamina = std::max(0.0f, status.stamina - STAMINA_DRAIN * deltaTime);
        staminaRest = STAMINA_PAUSE;
    } else {
        staminaRest -= deltaTime;
        if (staminaRest <= 0.0f) status.stamina = std::min(STAMINA_MAX, status.stamina + STAMINA_REGEN * deltaTime);
    }

    // A diver goes where the camera looks.
    if (character.diving) direction.y = lookDirection().y * std::clamp(input.moveForward, -1.0f, 1.0f);
    const float swimUp = input.dive ? -1.0f : (input.jump ? 1.0f : 0.0f);

    const CharacterMove result = controller.move(*player, character, direction, speed, input.jump, surroundings, deltaTime, swimUp);
    status.swimming = character.swimming;
    status.diving = character.diving;
    status.underwater = character.submerged;
    if (character.submerged) {
        if (const WaterArea* water = surroundings.waterAt(glm::vec2(player->position.x, player->position.z))) waterLevel = water->level;
    }

    // Air runs out under water and comes back above it; without it the player is hurt.
    status.maxBreath = BREATH_SECONDS;
    if (status.underwater) {
        status.breath = std::max(0.0f, status.breath - deltaTime);
        if (status.breath <= 0.0f && status.hasCombat) {
            drownClock += deltaTime;
            if (drownClock >= 1.0f) {
                drownClock = 0.0f;
                hurtPlayer(scene, DROWN_DAMAGE);
            }
        }
    } else {
        status.breath = std::min(status.maxBreath, status.breath + deltaTime * BREATH_RECOVERY);
        drownClock = 0.0f;
    }
    // Flames hurt whoever stands in them; being in water puts them out.
    bool inFlames = false;
    if (!character.swimming && status.hasCombat) {
        const glm::vec3 feet = playerFeet(scene);
        for (const auto& fire : scene.getFires()) {
            if (!fire->burning || fire->damage <= 0.0f) continue;
            const glm::vec3 at = fire->getWorldPosition() - feet;
            const float reach = std::max(0.6f, 0.45f * fire->size * std::max(fire->scale.x, fire->scale.z)) + 0.3f;
            const float height = std::max(1.0f, fire->size * fire->scale.y) + 0.5f;
            if (glm::length(glm::vec2(at.x, at.z)) < reach && feet.y > fire->getWorldPosition().y - 1.5f &&
                feet.y < fire->getWorldPosition().y + height) {
                burnClock += deltaTime;
                inFlames = true;
                if (burnClock >= 0.5f) {
                    burnClock = 0.0f;
                    hurtPlayer(scene, fire->damage * 0.5f);
                }
                break;
            }
        }
    }
    status.burning = inFlames;
    if (!inFlames) burnClock = 0.0f;
    status.grounded = character.grounded;
    status.moveSpeed = glm::length(glm::vec2(player->velocity.x, player->velocity.z));

    // A footstep for each stride on foot; on horseback the hooves are heard instead.
    if (character.grounded && !character.swimming && !status.mounted && status.moveSpeed > 0.5f) {
        stepDistance += status.moveSpeed * deltaTime;
        const float length = sprinting ? SPRINT_STEP_LENGTH : STEP_LENGTH;
        if (stepDistance >= length) {
            stepDistance = std::fmod(stepDistance, length);
            const glm::vec3 feet = playerFeet(scene);
            cue(SoundCue::Kind::Footstep, feet, surfaceUnder(scene, feet));
        }
    } else if (!character.grounded) {
        stepDistance = 0.0f;
    }

    if (status.hasCombat && result.landingSpeed > SAFE_FALL_SPEED) {
        hurtPlayer(scene, (result.landingSpeed - SAFE_FALL_SPEED) * FALL_DAMAGE_PER_SPEED);
    }

    pushProps(*player, direction * speed);
}

void GameSession::cue(SoundCue::Kind kind, glm::vec3 position, Surface surface) {
    cues.push_back({kind, position, surface});
}

// What the player is walking on: the floor of a building or a bridge if there is one under the
// feet, cobbles in the middle of a town, otherwise the terrain, which is snow up high and sand
// along the water.
Surface GameSession::surfaceUnder(const SceneGraph&, glm::vec3 feet) const {
    for (const Floor& floor : floors) {
        const glm::vec3 local = glm::vec3(floor.toLocal * glm::vec4(feet, 1.0f));
        if (local.x >= floor.low.x && local.x <= floor.high.x && local.z >= floor.low.z && local.z <= floor.high.z &&
            local.y >= floor.low.y - 0.4f && local.y <= floor.high.y) {
            return floor.surface;
        }
    }

    for (const glm::vec3& place : pavedPlaces) {
        if (glm::length(glm::vec2(place.x - feet.x, place.z - feet.z)) < PAVED_RADIUS) return Surface::Stone;
    }
    if (!surroundings.ground) return Surface::Stone;

    if (feet.y > SNOW_HEIGHT) return Surface::Snow;
    for (const WaterArea& water : surroundings.waters) {
        const bool inside = feet.x >= water.area.x && feet.x <= water.area.z && feet.z >= water.area.y && feet.z <= water.area.w;
        if (inside && feet.y < water.level + SHORE_MARGIN) return Surface::Sand;
    }
    return Surface::Grass;
}

void GameSession::pushProps(Shape& player, glm::vec3 wishVelocity) {
    if (glm::length(wishVelocity) < 0.1f) return;

    // The controller stops at a crate; a crate in the way is then given the player's pace.
    const glm::vec3 center = player.getWorldPosition();
    const glm::vec3 reach = 0.5f * player.getWorldScale() + glm::vec3(0.12f);
    for (Shape* prop : props) {
        const glm::vec3 offset = prop->getWorldPosition() - center;
        const glm::vec3 gap = glm::abs(offset) - 0.5f * prop->getWorldScale() - reach;
        if (gap.x > 0.0f || gap.y > 0.0f || gap.z > 0.0f) continue;
        if (glm::dot(glm::vec2(offset.x, offset.z), glm::vec2(wishVelocity.x, wishVelocity.z)) <= 0.0f) continue;

        prop->velocity.x = wishVelocity.x * PUSH_TRANSFER;
        prop->velocity.z = wishVelocity.z * PUSH_TRANSFER;
    }
}

void GameSession::mount(SceneGraph& scene, Shape& horse) {
    Player* player = scene.getPlayer();
    status.mounted = true;
    cue(SoundCue::Kind::Mount, horse.getWorldPosition());
    horseId = horse.id;
    player->setPosition(glm::vec3(horse.position.x, player->position.y, horse.position.z));
}

void GameSession::dismount(SceneGraph& scene) {
    Player* player = scene.getPlayer();
    status.mounted = false;
    cue(SoundCue::Kind::Dismount, player->position);
    horseId = InvalidObjectId;
    const glm::vec3 forward = cameraForward(cameraYaw);
    player->setPosition(player->position + glm::vec3(-forward.z, 0.0f, forward.x) * 1.5f);
}

void GameSession::updateHorse(SceneGraph& scene) {
    Player* player = scene.getPlayer();
    Shape* ridden = scene.findAs<Shape>(horseId);
    if (!ridden) return;

    const glm::vec2 move(player->velocity.x, player->velocity.z);
    const float speed = glm::length(move);
    const glm::vec3 feet = playerFeet(scene);
    ridden->setPosition(glm::vec3(player->position.x, feet.y + std::abs(std::sin(stride * 1.6f)) * 0.12f, player->position.z));
    if (speed > 0.1f) {
        ridden->setRotation(glm::vec3(0.0f, glm::degrees(std::atan2(-move.x, -move.y)), 0.0f));
    }
    animateLimbs(*ridden, stride * 1.6f, std::min(1.0f, speed / 6.0f));
}

void GameSession::updateIdleHorses(SceneGraph& scene, float deltaTime) {
    const float time = scene.getAnimationTime();
    Player* player = scene.getPlayer();
    const glm::vec3 pFeet = player ? player->position : glm::vec3(0.0f);

    for (ObjectId id : horses) {
        Shape* horse = scene.findAs<Shape>(id);
        if (!horse) continue;

        const float phase = float(id) * 1.618f;

        // Grazing / breathing idle: gentle neck/head nodding and subtle body sway
        glm::vec3 rot = horse->rotation;
        float nod = std::sin(time * 0.7f + phase);
        rot.x = nod * 2.5f;
        rot.z = std::sin(time * 0.4f + phase) * 0.8f;

        // Hoof weight-shifting idle
        float legIdle = std::max(0.0f, std::sin(time * 0.5f + phase) - 0.7f) * 3.33f;
        animateLimbs(*horse, time * 1.5f + phase, legIdle * 0.25f);

        // Player awareness: if player is within 6m, horse turns gently to watch player
        if (player) {
            glm::vec3 toPlayer = pFeet - horse->position;
            float dist = glm::length(glm::vec2(toPlayer.x, toPlayer.z));
            if (dist < 6.0f && dist > 0.4f) {
                float targetYaw = glm::degrees(std::atan2(-toPlayer.x, -toPlayer.z));
                float diff = targetYaw - rot.y;
                while (diff > 180.0f) diff -= 360.0f;
                while (diff < -180.0f) diff += 360.0f;
                if (std::abs(diff) < 80.0f) {
                    rot.y += diff * std::min(1.0f, 2.0f * deltaTime);
                }
            }
        }
        horse->setRotation(rot);
    }
}

void GameSession::updateInteraction(SceneGraph& scene, const GameInput& input) {
    status.prompt.clear();

    if (progress.getDialogue().open) {
        if (input.number > 0) {
            progress.choose(input.number - 1);
        } else if (input.interact) {
            progress.closeDialogue();
        }
        auto partner = named.find(progress.getPartner());
        if (partner != named.end()) actors.holdStill(partner->second, playerFeet(scene), 0.3f);
        return;
    }

    if (status.mounted) {
        status.prompt = "Get down";
        if (input.interact) dismount(scene);
        return;
    }

    const glm::vec3 feet = playerFeet(scene);
    const glm::vec3 forward = cameraForward(cameraYaw);

    Shape* best = nullptr;
    float bestScore = 1e9f;
    for (ObjectId id : interactables) {
        Shape* shape = scene.findAs<Shape>(id);
        if (!shape || !shape->isVisibleInHierarchy()) continue;
        if (shape->gameRole == GameRole::Villager && !actors.isAlive(id)) continue;
        if (shape->gameRole == GameRole::Chest && openedChests.count(id)) continue;

        const glm::vec3 offset = shape->getWorldPosition() - feet;
        const float distance = glm::length(glm::vec2(offset.x, offset.z));
        const float range = shape->gameRole == GameRole::Horse ? MOUNT_RANGE : INTERACT_RANGE;
        if (distance > range || std::abs(offset.y) > 3.0f) continue;

        // What is in front of the player wins over what is merely close.
        const glm::vec2 toward = distance > 0.01f ? glm::vec2(offset.x, offset.z) / distance : glm::vec2(0.0f);
        const float score = distance - glm::dot(toward, glm::vec2(forward.x, forward.z));
        if (score < bestScore) {
            bestScore = score;
            best = shape;
        }
    }
    if (!best) return;

    switch (best->gameRole) {
        case GameRole::Horse:    status.prompt = "Ride"; break;
        case GameRole::Villager: status.prompt = "Talk to " + best->name; break;
        case GameRole::Item: {
            const std::vector<ItemStack> stacks = parseStacks(best->gameParameter);
            status.prompt = "Take " + (stacks.empty() ? best->name : progress.itemName(stacks.front().item));
            break;
        }
        case GameRole::Chest:    status.prompt = "Open " + best->name; break;
        case GameRole::Bed:      status.prompt = "Sleep"; break;
        case GameRole::Sign:     status.prompt = "Read " + best->name; break;
        default: break;
    }

    if (input.interact) interact(scene, *best);
}

void GameSession::interact(SceneGraph& scene, Shape& target) {
    switch (target.gameRole) {
        case GameRole::Horse:
            mount(scene, target);
            break;
        case GameRole::Villager:
            progress.openDialogue(target.name);
            actors.holdStill(target.id, playerFeet(scene), 0.5f);
            break;
        case GameRole::Item:
            for (const ItemStack& stack : parseStacks(target.gameParameter)) progress.give(stack.item, stack.count);
            target.visible = false;
            cue(SoundCue::Kind::Pickup, target.getWorldPosition());
            break;
        case GameRole::Chest: {
            const std::vector<ItemStack> contents = parseStacks(target.gameParameter);
            if (contents.empty()) progress.notify("It is empty.");
            for (const ItemStack& stack : contents) progress.give(stack.item, stack.count);
            openedChests.insert(target.id);
            cue(SoundCue::Kind::Pickup, target.getWorldPosition());
            break;
        }
        case GameRole::Bed:
            sleep(scene);
            break;
        case GameRole::Sign:
            progress.openMessage(target.name, target.gameParameter);
            break;
        default:
            break;
    }
}

void GameSession::sleep(SceneGraph& scene) {
    if (actors.hostileNear(scene, playerFeet(scene), REST_SAFE_RANGE)) {
        progress.notify("You cannot rest with enemies nearby.");
        return;
    }
    clock.sleepUntil(WAKE_HOUR);
    clock.apply(scene);
    updateNightGlow(scene);
    status.hour = clock.getHour();
    status.day = clock.getDay();
    status.night = clock.isNight();
    status.health = status.maxHealth;
    status.stamina = STAMINA_MAX;
    status.rested = 1.0f;
    progress.notify(clock.isRunning() ? "You slept until morning." : "You feel rested.");
}

glm::vec3 GameSession::lookDirection() const {
    const float pitch = glm::radians(cameraPitch);
    return cameraForward(cameraYaw) * std::cos(pitch) + glm::vec3(0.0f, std::sin(pitch), 0.0f);
}

void GameSession::updateCombat(SceneGraph& scene, const GameInput& input, float deltaTime) {
    Player* player = scene.getPlayer();
    const glm::vec3 feet = playerFeet(scene);

    const int weaponBefore = status.weapon;
    // 1 and 2 take the sword and the bow in hand; 3 leaves the hands empty.
    if (input.number == 1 && swordId != InvalidObjectId) status.weapon = 1;
    if (input.number == 2 && bowId != InvalidObjectId) status.weapon = 2;
    if (input.number == 3) status.weapon = 0;
    if (status.weapon != weaponBefore) cue(SoundCue::Kind::Equip, feet);

    if (input.usePotion) {
        const std::string potion = progress.bestPotion();
        if (potion.empty()) {
            progress.notify("No potions");
        } else if (status.health < status.maxHealth && progress.take(potion, 1)) {
            const ItemDefinition* item = progress.findItem(potion);
            status.health = std::min(status.maxHealth, status.health + (item ? item->heal : 0.0f));
            progress.notify("You drink " + progress.itemName(potion));
        }
    }

    attackCooldown = std::max(0.0f, attackCooldown - deltaTime);
    swing = std::max(0.0f, swing - deltaTime * 3.0f);

    if (status.hasCombat && status.weapon != 0 && input.attack && attackCooldown <= 0.0f) {
        if (status.weapon == 1) {
            swing = 1.0f;
            attackCooldown = SWORD_SECONDS;
            const float damage = progress.bestWeaponDamage(SWORD_DAMAGE) * progress.damageScale();
            const int hits = actors.strike(scene, feet, cameraForward(cameraYaw), SWORD_RANGE, damage);
            cue(hits > 0 ? SoundCue::Kind::SwordHit : SoundCue::Kind::SwordSwing, feet);
        } else if (progress.usesArrows() && !progress.take("arrow", 1)) {
            attackCooldown = BOW_SECONDS;
            progress.notify("No arrows left");
        } else {
            swing = 1.0f;
            attackCooldown = BOW_SECONDS;
            CameraObject* camera = scene.getMainCamera();
            const glm::vec3 look = lookDirection();
            const glm::vec3 eye = camera ? camera->getWorldPosition() : player->position;

            // Aims at what the crosshair is on, seen from the camera rather than the bow.
            const glm::vec3 origin = player->getCameraPosition() + look * 0.8f;
            actors.fireArrow(scene, origin, glm::normalize(eye + look * 45.0f - origin) * ARROW_SPEED,
                             ARROW_DAMAGE * progress.damageScale());
            cue(SoundCue::Kind::ArrowShot, feet);
        }
    }

    const float damage = actors.update(scene, surroundings, feet, status.night, deltaTime);
    creatureSounds.insert(creatureSounds.end(), actors.sounds.begin(), actors.sounds.end());
    actors.sounds.clear();
    if (glm::length(actors.playerPush) > 0.0f) {
        // A heavy blow throws the player back; the controller soon takes over again.
        player->velocity += actors.playerPush;
        if (actors.playerPush.y > 0.0f) character.grounded = false;
        actors.playerPush = glm::vec3(0.0f);
    }
    handleEvents();
    status.banditsLeft = actors.banditsAlive();

    // A world with quests has no single end; an arena of bandits ends when they are gone.
    if (status.banditsTotal > 0 && status.banditsLeft == 0 && !progress.hasQuests()) {
        status.won = true;
    }

    if (damage > 0.0f) hurtPlayer(scene, damage);
}

void GameSession::handleEvents() {
    for (const ActorEvent& event : actors.events) {
        if (!status.hasWorld) continue;
        progress.onKill(roleKey(event.role), event.name);

        // What a body leaves behind, if the game knows the item.
        switch (event.role) {
            case GameRole::Wolf:
                if (progress.findItem("wolf_pelt")) progress.give("wolf_pelt", 1);
                progress.addXp(15);
                break;
            case GameRole::Deer:
                if (progress.findItem("venison")) progress.give("venison", 1);
                progress.addXp(5);
                break;
            case GameRole::Boar:
                if (progress.findItem("boar_tusk")) progress.give("boar_tusk", 1);
                progress.addXp(20);
                break;
            case GameRole::Bear:
                if (progress.findItem("bear_pelt")) progress.give("bear_pelt", 1);
                progress.addXp(60);
                break;
            case GameRole::Rabbit:
                if (progress.findItem("rabbit_pelt")) progress.give("rabbit_pelt", 1);
                progress.addXp(2);
                break;
            case GameRole::Dragon:
                if (progress.findItem("dragon_scale")) progress.give("dragon_scale", 3);
                progress.give("gold", 150);
                progress.addXp(400);
                break;
            case GameRole::Bandit:
                progress.give("gold", 5 + (int)(std::fmod(event.position.x * 13.7f + event.position.z * 7.3f, 8.0f) + 8.0f) % 8);
                progress.addXp(30);
                break;
            default:
                break;
        }
    }
    actors.events.clear();

    // A new level brings more health, and a full recovery.
    if (progress.level != knownLevel) {
        knownLevel = progress.level;
        cue(SoundCue::Kind::LevelUp, status.listenerPosition);
        status.maxHealth = progress.maxHealth();
        status.health = status.maxHealth;
    }
}

void GameSession::updateQuests(SceneGraph& scene) {
    const glm::vec3 feet = playerFeet(scene);
    for (const std::string& target : progress.reachTargets()) {
        auto found = named.find(target);
        if (found == named.end()) continue;
        if (const SceneObject* place = scene.find(found->second)) {
            if (glm::length(place->getWorldPosition() - feet) < REACH_RANGE) progress.onReach(target);
        }
    }

    // Quest rewards can include a level, so a level gained here heals too.
    handleEvents();
}

void GameSession::applyRules(SceneGraph& scene) {
    Player* player = scene.getPlayer();

    status.goalLocked = false;

    for (const auto& shape : scene.getShapes()) {
        if (shape->gameRole == GameRole::None || !shape->isVisibleInHierarchy()) continue;
        if (shape->gameRole != GameRole::Pickup && shape->gameRole != GameRole::Goal && shape->gameRole != GameRole::Hazard) continue;
        if (!player->checkCollision(*shape)) continue;

        switch (shape->gameRole) {
            case GameRole::Pickup:
                shape->visible = false;
                status.collected++;
                cue(SoundCue::Kind::Pickup, shape->getWorldPosition());
                break;
            case GameRole::Goal:
                status.goalLocked = status.collected < status.total;
                status.won = !status.goalLocked;
                break;
            case GameRole::Hazard:
                respawn(scene);
                return;
            default:
                break;
        }
    }

    // On terrain nothing can fall out of the world, and a valley below the start is no fall.
    if (!scene.ground.isValid() && player->position.y < killHeight) {
        respawn(scene);
    }
}

void GameSession::updateNightGlow(SceneGraph& scene) {
    const bool night = clock.isNight() || (clock.isRunning() && (clock.getHour() > 19.0f || clock.getHour() < 6.0f));
    if (night == glowing) return;
    glowing = night;

    for (ObjectId id : nightGlows) {
        if (Shape* shape = scene.findAs<Shape>(id)) {
            shape->emissive = night;
            shape->setColor(night ? glm::vec3(1.0f, 0.78f, 0.42f) : glm::vec3(0.55f, 0.6f, 0.7f));
        }
    }
}

void GameSession::hurtPlayer(SceneGraph& scene, float damage) {
    status.health -= damage;
    cue(status.health > 0.0f ? SoundCue::Kind::PlayerHurt : SoundCue::Kind::Defeat, playerFeet(scene));
    if (status.health > 0.0f) return;

    const int lost = (int)(progress.gold * DEFEAT_GOLD_LOSS);
    progress.loseGold(lost);
    progress.notify(lost > 0 ? "You were defeated and lost " + std::to_string(lost) + " gold." : "You were defeated.", 6.0f);
    status.health = status.maxHealth;
    if (status.mounted) dismount(scene);
    respawn(scene);
}

void GameSession::respawn(SceneGraph& scene) {
    Player* player = scene.getPlayer();
    player->setPosition(spawnPoint);
    player->velocity = glm::vec3(0.0f);
    character = CharacterState();
    status.falls++;
}

void GameSession::moveCamera(SceneGraph& scene, float deltaTime) {
    Player* player = scene.getPlayer();
    CameraObject* camera = scene.getMainCamera();
    if (!player || !camera) return;

    const float rise = status.mounted ? MOUNTED_EYE_RISE : 0.0f;

    if (status.firstPerson) {
        glm::vec3 eye = player->getCameraPosition() + glm::vec3(0.0f, rise, 0.0f);
        const float eyeBlend = deltaTime > 0.0f ? 1.0f - std::exp(-EYE_FOLLOW * deltaTime) : 1.0f;
        eyeHeight += (eye.y - eyeHeight) * eyeBlend;
        eye.y = eyeHeight;

        const glm::vec3 look = lookDirection();
        camera->setWorldMatrix(glm::inverse(glm::lookAt(eye, eye + look, glm::vec3(0.0f, 1.0f, 0.0f))) * glm::mat4_cast(headRotation));
        return;
    }

    // Ease towards the player: on the first call, deltaTime 0 means no easing is wanted.
    const float blend = deltaTime > 0.0f ? 1.0f - std::exp(-CAMERA_FOLLOW * deltaTime) : 1.0f;
    cameraFocus += (player->position - cameraFocus) * blend;

    // Terrain worlds have mouse look; courses keep a fixed, readable angle.
    const glm::vec3 forward = cameraForward(cameraYaw);
    const glm::vec3 base = cameraFocus + glm::vec3(0.0f, rise, 0.0f);
    glm::vec3 eye;
    glm::vec3 target;
    if (scene.ground.isValid()) {
        const glm::vec3 look = lookDirection();
        const glm::vec3 pivot = base + glm::vec3(0.0f, 1.2f, 0.0f);
        eye = pivot - look * cameraReach(scene, pivot, -look, 6.5f);
        eye.y = std::max(eye.y, scene.ground.heightAt(eye.x, eye.z) + 0.5f);
        // A diver's camera goes under with them, or the water would be seen from above.
        if (status.underwater) eye.y = std::min(eye.y, std::max(waterLevel - CAMERA_WATER_GAP, scene.ground.heightAt(eye.x, eye.z) + 0.2f));
        target = eye + look;
    } else {
        eye = base - forward * CAMERA_DISTANCE + glm::vec3(0.0f, CAMERA_HEIGHT, 0.0f);
        target = base + forward * CAMERA_LOOK_AHEAD;
    }

    camera->setWorldMatrix(glm::inverse(glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f))) * glm::mat4_cast(headRotation));
}

float GameSession::cameraReach(const SceneGraph& scene, glm::vec3 from, glm::vec3 direction, float distance) const {
    // The nearest wall between the player and where the camera wants to be, as a ray against
    // each collider's box; the camera then stops just short of it instead of looking from inside.
    // Starting inside a box, as under an archway, is not a wall in the way.
    const float margin = 0.35f;
    float nearest = distance;
    const glm::vec3 to = from + direction * distance;
    const glm::vec2 low(std::min(from.x, to.x), std::min(from.z, to.z));
    const glm::vec2 high(std::max(from.x, to.x), std::max(from.z, to.z));
    scene.colliders.query(low - glm::vec2(1.0f), high + glm::vec2(1.0f), [&](const SolidBox& box) {
        float enter = 0.0f;
        if (box.shape->isStatic && box.rayEnter(from, direction, 0.15f, enter)) nearest = std::min(nearest, enter);
    });
    return std::max(0.6f, nearest - margin);
}

void GameSession::placeModels(SceneGraph& scene, float deltaTime) {
    Player* player = scene.getPlayer();
    CameraObject* camera = scene.getMainCamera();

    const float speed = glm::length(glm::vec2(player->velocity.x, player->velocity.z));
    stride += speed * deltaTime * 1.4f;

    const glm::vec3 feet = playerFeet(scene);
    const float rise = status.mounted ? MOUNTED_EYE_RISE : 0.0f;
    const float bob = speed > 0.1f && !status.mounted ? std::abs(std::sin(stride)) * 0.06f : 0.0f;

    if (Shape* body = scene.findAs<Shape>(bodyId)) {
        body->setPosition(feet + glm::vec3(0.0f, rise + bob, 0.0f));
        body->setRotation(glm::vec3(0.0f, -cameraYaw, 0.0f));
        animateLimbs(*body, stride, status.mounted ? 0.0f : std::min(1.0f, speed / 4.0f));
    }

    const glm::vec3 forward = cameraForward(cameraYaw);
    const glm::vec3 right(-forward.z, 0.0f, forward.x);

    // The sword is held in the right hand, wherever the arm is.
    Shape* rightArm = nullptr;
    if (Shape* body = scene.findAs<Shape>(bodyId)) {
        for (SceneObject* child : body->getChildren()) {
            if (child->name == "Right Arm") rightArm = dynamic_cast<Shape*>(child);
        }
    }
    const bool viewSword = swordViewId != InvalidObjectId && status.firstPerson;

    auto place = [&](ObjectId id, bool active) {
        Shape* model = scene.findAs<Shape>(id);
        if (!model) return;
        model->visible = active;
        if (!active || !camera) return;

        glm::mat4 anchor;
        if (id == swordId && rightArm && !status.firstPerson) {
            anchor = glm::translate(rightArm->getWorldMatrix(), glm::vec3(0.0f, -0.62f, 0.0f));
            anchor = glm::rotate(anchor, glm::radians(-35.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        } else if (status.firstPerson) {
            // Held in the lower right of the view.
            anchor = glm::translate(camera->getWorldMatrix(), id == swordViewId ? glm::vec3(0.30f, -0.30f, -0.62f) : glm::vec3(0.34f, -0.34f, -0.7f));
            anchor = glm::rotate(anchor, glm::radians(-18.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            anchor = glm::scale(anchor, glm::vec3(0.55f));
        } else {
            anchor = glm::translate(glm::mat4(1.0f), feet + glm::vec3(0.0f, 1.05f + rise, 0.0f) + right * 0.5f + forward * 0.15f);
            anchor = glm::rotate(anchor, glm::radians(-cameraYaw), glm::vec3(0.0f, 1.0f, 0.0f));
        }

        if (id == swordId || id == swordViewId) {
            anchor = glm::rotate(anchor, glm::radians(-95.0f * swing), glm::vec3(1.0f, 0.0f, 0.0f));
        } else {
            anchor = glm::translate(anchor, glm::vec3(0.0f, 0.0f, 0.18f * swing));
        }
        model->setWorldMatrix(anchor);
    };

    place(swordId, status.weapon == 1 && !viewSword);
    place(swordViewId, status.weapon == 1 && viewSword);
    place(bowId, status.weapon == 2);
}
