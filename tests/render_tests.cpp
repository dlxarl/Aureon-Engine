// Tests that need an OpenGL context. Each render setting is exercised on the real pipeline,
// so that a control in the Render Settings panel cannot quietly stop doing anything.
#include "AssetManager.h"
#include "CharacterController.h"
#include "Colliders.h"
#include "EffectObjects.h"
#include "EditorCamera.h"
#include "GameClock.h"
#include "GameProgress.h"
#include "GameSession.h"
#include "Terrain.h"
#include "UiObject.h"
#include "GlWindow.h"
#include "MeshShape.h"
#include "ObjectFactory.h"
#include "Physics.h"
#include "PlayController.h"
#include "Player.h"
#include "RenderTarget.h"
#include "Renderer.h"
#include "Resources.h"
#include "SceneDocument.h"
#include "SceneGraph.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "ViewportRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <set>
#include <vector>

namespace fs = std::filesystem;

namespace {
    const int WIDTH = 320;
    const int HEIGHT = 240;

    using Image = std::vector<unsigned char>;

    // An 8x8 checkerboard as an uncompressed TGA, the simplest format stb_image reads.
    void writeCheckerTexture(const fs::path& path) {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);

        unsigned char header[18] = {};
        header[2] = 2;
        header[12] = 8;
        header[14] = 8;
        header[16] = 24;
        file.write(reinterpret_cast<const char*>(header), sizeof(header));

        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                const unsigned char value = ((x + y) & 1) ? 240 : 30;
                const unsigned char pixel[3] = {value, value, value};
                file.write(reinterpret_cast<const char*>(pixel), 3);
            }
        }
    }

    Shape* addShape(SceneGraph& scene, ObjectType type, glm::vec3 position, glm::vec3 scale, glm::vec3 color) {
        auto object = ObjectFactory::create(type);
        object->name = ObjectFactory::typeName(type);
        object->setPosition(position);
        object->setScale(scale);
        auto* shape = static_cast<Shape*>(scene.add(object));
        shape->setColor(color);
        return shape;
    }

    // A tilted textured floor, which shows filtering and affine warping, and objects that
    // catch light and throw shadows.
    void buildScene(SceneGraph& scene, AssetManager& assets) {
        Shape* floor = addShape(scene, ObjectType::Plane, glm::vec3(0.0f), glm::vec3(16.0f, 0.1f, 16.0f), glm::vec3(1.0f));
        floor->setTexture("texture_albedo", assets.getTexture("Assets/Textures/checker.tga"));

        addShape(scene, ObjectType::Cube, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.5f), glm::vec3(0.9f, 0.4f, 0.2f));
        addShape(scene, ObjectType::Sphere, glm::vec3(2.5f, 1.0f, 1.0f), glm::vec3(1.8f), glm::vec3(0.2f, 0.5f, 0.9f));

        auto light = ObjectFactory::create(ObjectType::Light);
        light->setPosition(glm::vec3(6.0f, 10.0f, 4.0f));
        static_cast<LightObject*>(light.get())->type = LightType::Directional;
        light->setWorldMatrix(glm::inverse(glm::lookAt(glm::vec3(6.0f, 10.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))));
        scene.add(light);
    }

    CameraView makeCamera(glm::vec3 eye) {
        CameraView camera;
        camera.position = eye;
        camera.view = glm::lookAt(eye, glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        camera.projection = glm::perspective(glm::radians(50.0f), (float)WIDTH / HEIGHT, 0.1f, 200.0f);
        return camera;
    }

    Image readImage(unsigned int framebuffer) {
        Image pixels(WIDTH * HEIGHT * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return pixels;
    }

    int differingPixels(const Image& a, const Image& b) {
        int count = 0;
        for (size_t i = 0; i < a.size(); i += 4) {
            if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) count++;
        }
        return count;
    }

    int distinctChannelValues(const Image& image) {
        std::set<unsigned char> values;
        for (size_t i = 0; i < image.size(); i += 4) {
            values.insert(image[i]);
            values.insert(image[i + 1]);
            values.insert(image[i + 2]);
        }
        return (int)values.size();
    }

    int identicalAdjacentRows(const Image& image) {
        int identical = 0;
        for (int y = 0; y + 1 < HEIGHT; y++) {
            const unsigned char* a = image.data() + (size_t)y * WIDTH * 4;
            const unsigned char* b = a + WIDTH * 4;
            if (std::equal(a, a + WIDTH * 4, b)) identical++;
        }
        return identical;
    }

    class RenderFixture {
    public:
        RenderFixture(AssetManager& assets) : renderer(1024) {
            target.resize(WIDTH, HEIGHT);
            buildScene(scene, assets);
        }

        Image render(const RenderSettings& settings, glm::vec3 eye = glm::vec3(5.0f, 5.0f, 9.0f)) {
            renderer.render(scene, makeCamera(eye), RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, settings);
            return readImage(target.framebuffer());
        }

        SceneGraph scene;
        Renderer renderer;
        RenderTarget target;
    };

    void testRenderSettings(AssetManager& assets) {
        RenderFixture fixture(assets);
        const RenderSettings base;
        const Image reference = fixture.render(base);

        CHECK(distinctChannelValues(reference) > 32);

        // Deterministic, or every comparison below would prove nothing.
        CHECK(differingPixels(reference, fixture.render(base)) == 0);

        // Internal resolution: rows repeat when a low-resolution image is upscaled.
        RenderSettings lowRes = base;
        lowRes.internalHeight = 60;
        const Image lowResImage = fixture.render(lowRes);
        CHECK(differingPixels(reference, lowResImage) > 500);
        CHECK(identicalAdjacentRows(lowResImage) > identicalAdjacentRows(reference) + 100);

        // Texture filtering
        RenderSettings nearest = base;
        nearest.textureFilter = TextureFilter::Nearest;
        RenderSettings bilinear = base;
        bilinear.textureFilter = TextureFilter::Bilinear;
        const Image nearestImage = fixture.render(nearest);
        CHECK(differingPixels(reference, nearestImage) > 200);
        CHECK(differingPixels(nearestImage, fixture.render(bilinear)) > 200);

        // Color quantization: 3 bits per channel can only produce 8 values.
        RenderSettings quantized = base;
        quantized.colorBits = 3;
        const Image quantizedImage = fixture.render(quantized);
        CHECK(distinctChannelValues(quantizedImage) <= 8);
        CHECK(differingPixels(reference, quantizedImage) > 1000);

        // Dithering
        RenderSettings dithered = quantized;
        dithered.dithering = true;
        const Image ditheredImage = fixture.render(dithered);
        CHECK(differingPixels(quantizedImage, ditheredImage) > 1000);
        CHECK(distinctChannelValues(ditheredImage) <= 8);

        // Dithering does nothing without quantization, which is why the panel disables it.
        RenderSettings ditherOnly = base;
        ditherOnly.dithering = true;
        CHECK(differingPixels(reference, fixture.render(ditherOnly)) == 0);

        // Vertex precision
        RenderSettings snapped = base;
        snapped.vertexPrecision = VertexPrecision::Low;
        CHECK(differingPixels(reference, fixture.render(snapped)) > 200);

        // Rendering mode: per-vertex lighting, and affine against perspective-correct textures.
        RenderSettings ps2 = base;
        ps2.mode = RenderMode::PS2;
        RenderSettings ps1 = base;
        ps1.mode = RenderMode::PS1;
        const Image ps2Image = fixture.render(ps2);
        CHECK(differingPixels(reference, ps2Image) > 500);
        CHECK(differingPixels(ps2Image, fixture.render(ps1)) > 500);

        // Lighting and shadows
        RenderSettings unlit = base;
        unlit.lighting = false;
        CHECK(differingPixels(reference, fixture.render(unlit)) > 1000);

        RenderSettings noShadows = base;
        noShadows.shadows = false;
        CHECK(differingPixels(reference, fixture.render(noShadows)) > 100);

        // Motion blur needs movement between frames.
        const glm::vec3 eyeA(5.0f, 5.0f, 9.0f);
        const glm::vec3 eyeB(5.6f, 5.0f, 8.6f);
        fixture.render(base, eyeA);
        const Image sharp = fixture.render(base, eyeB);
        RenderSettings blurred = base;
        blurred.motionBlur = true;
        fixture.render(blurred, eyeA);
        CHECK(differingPixels(sharp, fixture.render(blurred, eyeB)) > 200);

        // A disabled shadow pass must not leave the scene black, and the presets must render.
        for (RenderMode mode : {RenderMode::PS1, RenderMode::PS2, RenderMode::Modern}) {
            CHECK(distinctChannelValues(fixture.render(RenderSettings::preset(mode))) > 3);
        }
    }

    // The post-processing stack: every effect changes the picture, turning it off restores the
    // picture exactly, and the settings survive a round trip through JSON.
    void testPostProcessing(AssetManager& assets) {
        RenderFixture fixture(assets);
        const RenderSettings base;
        const Image reference = fixture.render(base);
        while (glGetError() != GL_NO_ERROR) {}

        struct Case { const char* name; void (*apply)(PostSettings&); int least; };
        const Case cases[] = {
            {"aces", [](PostSettings& p) { p.toneMapper = ToneMapper::ACES; }, 200},
            {"reinhard", [](PostSettings& p) { p.toneMapper = ToneMapper::Reinhard; }, 200},
            {"neutral", [](PostSettings& p) { p.toneMapper = ToneMapper::Neutral; }, 20},
            {"adjustments", [](PostSettings& p) { p.colorAdjustments = true; p.exposure = 1.0f; p.contrast = 0.4f; p.colorFilter = glm::vec3(1.0f, 0.7f, 0.5f); }, 200},
            {"white balance", [](PostSettings& p) { p.whiteBalance = true; p.temperature = 0.8f; p.tint = 0.4f; }, 200},
            {"split toning", [](PostSettings& p) { p.splitToning = true; p.shadowsTint = glm::vec3(0.2f, 0.3f, 0.9f); p.highlightsTint = glm::vec3(0.9f, 0.6f, 0.2f); }, 200},
            {"bloom", [](PostSettings& p) { p.bloom = true; p.bloomThreshold = 0.1f; p.bloomIntensity = 1.5f; }, 200},
            {"depth of field", [](PostSettings& p) { p.depthOfField = true; p.focalDistance = 2.0f; p.focalRange = 1.0f; p.maxBlurRadius = 8.0f; }, 200},
            {"distortion", [](PostSettings& p) { p.lensDistortion = true; p.distortion = 0.8f; }, 200},
            {"ssao", [](PostSettings& p) { p.ssao = true; p.ssaoRadius = 1.5f; p.ssaoIntensity = 2.0f; p.ssaoSamples = 32; }, 20},
            {"fxaa", [](PostSettings& p) { p.fxaa = true; }, 5},
        };

        for (const Case& c : cases) {
            RenderSettings on = base;
            c.apply(on.post);
            const int differing = differingPixels(reference, fixture.render(on));
            if (differing < c.least) std::printf("  post effect '%s' changed %d pixels\n", c.name, differing);
            CHECK(differing >= c.least);
            CHECK(glGetError() == GL_NO_ERROR);

            // Off again, the picture is the one it was: nothing is left running.
            CHECK(differingPixels(reference, fixture.render(base)) == 0);
        }

        // All of them at once, in HDR with a normal buffer, then back to plain.
        RenderSettings everything = base;
        for (const Case& c : cases) c.apply(everything.post);
        everything.motionBlur = true;
        everything.vignette = 0.5f;
        everything.post.vignetteRoundness = 1.0f;
        everything.post.vignetteSmoothness = 0.3f;
        const Image stacked = fixture.render(everything);
        CHECK(distinctChannelValues(stacked) > 16);
        CHECK(glGetError() == GL_NO_ERROR);
        fixture.render(base);
        CHECK(differingPixels(reference, fixture.render(base)) == 0);

        // The stack works at a lower internal resolution too, and with a retro mode on top.
        RenderSettings retro = RenderSettings::preset(RenderMode::PS2);
        retro.post.toneMapper = ToneMapper::ACES;
        retro.post.bloom = true;
        retro.post.ssao = true;
        retro.post.fxaa = true;
        CHECK(distinctChannelValues(fixture.render(retro)) > 3);
        CHECK(glGetError() == GL_NO_ERROR);

        // Settings round trip, and out-of-range values are brought back.
        RenderSettings saved;
        saved.post.toneMapper = ToneMapper::Neutral;
        saved.post.exposure = 1.5f;
        saved.post.colorFilter = glm::vec3(1.0f, 0.5f, 0.25f);
        saved.post.splitToning = true;
        saved.post.shadowsTint = glm::vec3(0.25f, 0.5f, 0.75f);
        saved.post.bloom = true;
        saved.post.bloomThreshold = 1.25f;
        saved.post.depthOfField = true;
        saved.post.focalDistance = 12.0f;
        saved.post.ssao = true;
        saved.post.ssaoSamples = 32;
        saved.post.fxaa = true;
        saved.post.distortion = -0.5f;
        saved.post.vignetteRoundness = 0.75f;
        RenderSettings loaded;
        CHECK(RenderSettings::fromJson(saved.toJson(), loaded));
        CHECK(loaded.post.toneMapper == ToneMapper::Neutral && loaded.post.exposure == 1.5f);
        CHECK(loaded.post.colorFilter == glm::vec3(1.0f, 0.5f, 0.25f) && loaded.post.splitToning);
        CHECK(loaded.post.shadowsTint == glm::vec3(0.25f, 0.5f, 0.75f) && loaded.post.bloom && loaded.post.bloomThreshold == 1.25f);
        CHECK(loaded.post.depthOfField && loaded.post.focalDistance == 12.0f && loaded.post.ssao && loaded.post.ssaoSamples == 32);
        CHECK(loaded.post.fxaa && loaded.post.distortion == -0.5f && loaded.post.vignetteRoundness == 0.75f);
        CHECK(RenderSettings::fromJson(R"({"post": {"exposure": 99, "maxBlurRadius": -4, "ssaoSamples": 7, "toneMapper": "aces"}})", loaded));
        CHECK(loaded.post.exposure == 5.0f && loaded.post.maxBlurRadius == 0.5f && loaded.post.ssaoSamples == 16);
        CHECK(loaded.post.toneMapper == ToneMapper::ACES);

        // A project from before the stack has none of it on.
        CHECK(RenderSettings::fromJson(R"({"renderMode": "ps2"})", loaded));
        CHECK(!loaded.post.needsGrade() && !loaded.post.ssao && !loaded.post.fxaa && !loaded.post.depthOfField);
    }

    void testViewportRenderer(AssetManager& assets) {
        SceneGraph scene;
        buildScene(scene, assets);

        ViewportRenderer viewport;
        viewport.resize(WIDTH, HEIGHT);

        const CameraView camera = makeCamera(glm::vec3(0.0f, 1.0f, 9.0f));

        Shape* cube = nullptr;
        for (const auto& shape : scene.getShapes()) {
            if (shape->getType() == ObjectType::Cube) cube = shape.get();
        }
        CHECK(cube != nullptr);
        if (!cube) return;

        // The cube sits in the middle of this view; well above it there is only background.
        CHECK(viewport.pick(scene, camera, WIDTH / 2, HEIGHT / 2) == cube->id);
        CHECK(viewport.pick(scene, camera, WIDTH / 2, 8) == InvalidObjectId);
        CHECK(viewport.pick(scene, camera, -1, 0) == InvalidObjectId);
        CHECK(viewport.pick(scene, camera, WIDTH, HEIGHT) == InvalidObjectId);

        cube->visible = false;
        CHECK(viewport.pick(scene, camera, WIDTH / 2, HEIGHT / 2) != cube->id);
        cube->visible = true;

        // The editor camera frames the origin in both projections.
        EditorCamera editorCamera;
        const float aspect = (float)WIDTH / HEIGHT;
        CHECK(viewport.pick(scene, editorCamera.makeView(aspect), WIDTH / 2, HEIGHT / 2) == cube->id);
        editorCamera.orthographic = true;
        const CameraView orthographic = editorCamera.makeView(aspect);
        CHECK(orthographic.projection[3][3] == 1.0f);
        CHECK(viewport.pick(scene, orthographic, WIDTH / 2, HEIGHT / 2) == cube->id);

        const RenderSettings settings;
        viewport.renderScene(scene, camera, settings, InvalidObjectId);

        // The image is drawn in a panel with blending, so its alpha has to stay opaque.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        Image pixels(WIDTH * HEIGHT * 4);
        glBindTexture(GL_TEXTURE_2D, viewport.texture());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        bool opaque = true;
        for (size_t i = 3; i < pixels.size(); i += 4) {
            opaque = opaque && pixels[i] == 255;
        }
        CHECK(opaque);

        // The outline adds the accent color around the selection, and nothing else moves.
        viewport.renderScene(scene, camera, settings, cube->id);
        Image outlined(WIDTH * HEIGHT * 4);
        glBindTexture(GL_TEXTURE_2D, viewport.texture());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, outlined.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        const int changed = differingPixels(pixels, outlined);
        CHECK(changed > 50);
        CHECK(changed < WIDTH * HEIGHT / 8);
    }

    void testHierarchy() {
        SceneGraph scene;

        auto makeEmpty = [&](glm::vec3 position) {
            auto object = ObjectFactory::create(ObjectType::Empty);
            object->setPosition(position);
            return scene.add(object);
        };

        SceneObject* a = makeEmpty(glm::vec3(10.0f, 0.0f, 0.0f));
        SceneObject* b = makeEmpty(glm::vec3(0.0f, 5.0f, 0.0f));
        SceneObject* c = makeEmpty(glm::vec3(1.0f, 1.0f, 1.0f));

        CHECK(a->id != b->id);
        CHECK(scene.getRoots().size() == 3);

        // Reparenting keeps the object where it is.
        CHECK(scene.reparent(b->id, a->id));
        CHECK(glm::length(b->getWorldPosition() - glm::vec3(0.0f, 5.0f, 0.0f)) < 0.0001f);
        CHECK(glm::length(b->position - glm::vec3(-10.0f, 5.0f, 0.0f)) < 0.0001f);
        CHECK(scene.getRoots().size() == 2);

        // Children follow their parent, through more than one level.
        CHECK(scene.reparent(c->id, b->id));
        a->setPosition(glm::vec3(20.0f, 0.0f, 0.0f));
        CHECK(glm::length(c->getWorldPosition() - glm::vec3(11.0f, 1.0f, 1.0f)) < 0.0001f);

        // Cycles are refused.
        CHECK(!scene.reparent(a->id, c->id));
        CHECK(!scene.reparent(a->id, a->id));
        CHECK(a->getParent() == nullptr);

        // Reordering among siblings leaves the transform untouched.
        SceneObject* d = makeEmpty(glm::vec3(0.0f));
        CHECK(scene.getRoots().back() == d);
        CHECK(scene.reparent(d->id, InvalidObjectId, 0));
        CHECK(scene.getRoots().front() == d);

        CHECK(scene.makeUniqueName("Cube") == "Cube");

        // Removing a parent removes its subtree.
        const ObjectId bId = b->id;
        const ObjectId cId = c->id;
        scene.remove(a->id);
        CHECK(scene.find(bId) == nullptr);
        CHECK(scene.find(cId) == nullptr);
        CHECK(scene.getObjects().size() == 1);
        CHECK(scene.getRoots().size() == 1);
    }

    void testTransformDecomposition() {
        const glm::vec3 cases[] = {{0, 0, 0}, {10, 20, 30}, {-45, 80, 170}, {90, 0, 0}, {0, 45, 0}, {120, -30, 60}};
        for (const glm::vec3& rotation : cases) {
            auto object = ObjectFactory::create(ObjectType::Empty);
            object->setPosition(glm::vec3(1.0f, 2.0f, 3.0f));
            object->setRotation(rotation);
            object->setScale(glm::vec3(2.0f, 3.0f, 0.5f));

            const glm::mat4 original = object->getWorldMatrix();
            object->setWorldMatrix(original);

            bool same = true;
            for (int column = 0; column < 4; column++) {
                same = same && glm::length(original[column] - object->getWorldMatrix()[column]) < 0.001f;
            }
            CHECK(same);
        }
    }

    void testSerialization(AssetManager& assets) {
        SceneGraph scene;
        buildScene(scene, assets);
        scene.clearColor = glm::vec3(0.2f, 0.3f, 0.4f);

        SceneObject* cube = scene.getShapes()[1].get();
        cube->name = "Кубик";
        cube->visible = false;
        cube->setRotation(glm::vec3(10.0f, 20.0f, 30.0f));
        static_cast<Shape*>(cube)->castShadow = false;
        CHECK(scene.reparent(cube->id, scene.getShapes()[0]->id));

        const std::string first = SceneSerializer::serialize(scene);

        SceneGraph loaded;
        std::string error;
        CHECK(SceneSerializer::deserialize(first, loaded, assets, &error));
        CHECK(loaded.getObjects().size() == scene.getObjects().size());

        // Saving what was loaded gives the identical file, so reloading loses nothing.
        CHECK(SceneSerializer::serialize(loaded) == first);

        SceneObject* restored = loaded.find(cube->id);
        CHECK(restored != nullptr);
        if (restored) {
            CHECK(restored->name == "Кубик");
            CHECK(!restored->visible);
            CHECK(restored->getParent() != nullptr);
            CHECK(glm::length(restored->getWorldPosition() - cube->getWorldPosition()) < 0.0001f);
        }
        CHECK(loaded.getShapes()[0]->getTexture("texture_albedo") != nullptr);
        CHECK(loaded.getShapes()[0]->getTexture("texture_albedo")->assetPath == "Assets/Textures/checker.tga");
        CHECK(glm::length(loaded.clearColor - scene.clearColor) < 0.0001f);

        // New objects never reuse a saved id.
        SceneObject* extra = loaded.add(ObjectFactory::create(ObjectType::Empty));
        CHECK(scene.find(extra->id) == nullptr);

        // Damaged files are refused without crashing.
        for (const char* broken : {"", "{", "{\"version\":1}", "{\"version\":99,\"objects\":[]}",
                                   R"({"version":1,"objects":[{"id":1,"type":"cube"},{"id":1,"type":"cube"}]})",
                                   R"({"version":1,"objects":[{"id":0,"type":"cube"}]})"}) {
            SceneGraph target;
            CHECK(!SceneSerializer::deserialize(broken, target, assets, &error));
            CHECK(target.getObjects().empty());
        }

        // Unknown object types are skipped, and a parent cycle is broken, not looped on.
        SceneGraph tolerant;
        CHECK(SceneSerializer::deserialize(
            R"({"version":1,"objects":[{"id":1,"type":"hologram"},{"id":2,"type":"cube"},
                {"id":3,"type":"empty","parent":4},{"id":4,"type":"empty","parent":3}]})",
            tolerant, assets, &error));
        CHECK(tolerant.getObjects().size() == 3);
    }

    void testDuplicate(AssetManager& assets) {
        SceneGraph scene;
        buildScene(scene, assets);

        SceneObject* parent = scene.getShapes()[1].get();
        SceneObject* child = scene.getShapes()[2].get();
        CHECK(scene.reparent(child->id, parent->id));
        const size_t before = scene.getObjects().size();

        SceneObject* copy = SceneSerializer::duplicate(scene, parent->id, assets);
        CHECK(copy != nullptr);
        if (!copy) return;

        CHECK(copy->id != parent->id);
        CHECK(copy->name == parent->name + " (1)");
        CHECK(scene.getObjects().size() == before + 2);
        CHECK(copy->getChildren().size() == 1);
        CHECK(copy->getChildren()[0]->id != child->id);
        CHECK(glm::length(copy->getWorldPosition() - parent->getWorldPosition()) < 0.0001f);

        // Sits right after the original.
        const auto& roots = scene.getRoots();
        const auto original = std::find(roots.begin(), roots.end(), parent);
        CHECK(original != roots.end() && *(original + 1) == copy);
    }

    void testMeshAssets(AssetManager& assets, const fs::path& root) {
        {
            fs::create_directories(root / "Assets/Models");
            std::ofstream file(root / "Assets/Models/tri.obj");
            file << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
        }

        CHECK(assets.getMesh("Assets/Models/tri.obj") != nullptr);
        CHECK(assets.getMesh("Assets/Models/missing.obj") == nullptr);
        CHECK(assets.getMesh("../escape.obj") == nullptr);

        // The parsed result lands in the cache, and survives the source being re-read.
        bool cached = false;
        for (const auto& entry : fs::directory_iterator(root / "Library/Meshes")) {
            cached = cached || entry.path().extension() == ".mesh";
        }
        CHECK(cached);

        assets.invalidate("Assets/Models/tri.obj");
        CHECK(assets.getMesh("Assets/Models/tri.obj") != nullptr);

        auto object = std::make_shared<MeshShape>();
        object->setMesh(assets.getMesh("Assets/Models/tri.obj"), "Assets/Models/tri.obj");
        SceneGraph scene;
        scene.add(object);

        SceneGraph loaded;
        std::string error;
        CHECK(SceneSerializer::deserialize(SceneSerializer::serialize(scene), loaded, assets, &error));
        auto* restored = dynamic_cast<MeshShape*>(loaded.getShapes()[0].get());
        CHECK(restored && restored->getAssetPath() == "Assets/Models/tri.obj");
    }

    void testPhysics() {
        SceneGraph scene;
        Shape* floor = addShape(scene, ObjectType::Plane, glm::vec3(0.0f), glm::vec3(10.0f, 0.1f, 10.0f), glm::vec3(1.0f));
        Shape* box = addShape(scene, ObjectType::Cube, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f), glm::vec3(1.0f));
        box->useGravity = true;

        for (int i = 0; i < 240; i++) {
            scene.update(1.0f / 120.0f);
        }

        CHECK(std::abs(box->position.y - 0.55f) < 0.1f);
        CHECK(box->position.y >= 0.55f);
        CHECK(floor->position.y == 0.0f);

        // A static object ignores gravity.
        box->isStatic = true;
        box->setPosition(glm::vec3(0.0f, 5.0f, 0.0f));
        scene.update(0.1f);
        CHECK(box->position.y == 5.0f);
    }

    // A flat strip along -Z with the player at its near end, and a camera to follow it.
    struct GameLevel {
        SceneGraph scene;
        Player* player = nullptr;

        GameLevel() {
            addShape(scene, ObjectType::Cube, glm::vec3(0.0f, -0.5f, -10.0f), glm::vec3(10.0f, 1.0f, 30.0f), glm::vec3(1.0f))->isStatic = true;

            auto playerObject = ObjectFactory::create(ObjectType::Player);

            static_cast<Player*>(playerObject.get())->gameControls = true;
            playerObject->setPosition(glm::vec3(0.0f, 1.2f, 0.0f));
            player = static_cast<Player*>(scene.add(playerObject));

            scene.add(ObjectFactory::create(ObjectType::Camera));
        }

        Shape* addRole(GameRole role, glm::vec3 position) {
            Shape* shape = addShape(scene, ObjectType::Cube, position, glm::vec3(1.0f), glm::vec3(1.0f));
            shape->gameRole = role;
            shape->isStatic = true;
            return shape;
        }

        void run(GameSession& game, const GameInput& input, float seconds) {
            const float step = 1.0f / 120.0f;
            for (float time = 0.0f; time < seconds; time += step) {
                game.update(scene, input, step);
                scene.update(step);
            }
        }
    };

    void testGameRules() {
        GameInput forward;
        forward.moveForward = 1.0f;

        {
            // A scene without a player is simply not a game.
            SceneGraph scene;
            GameSession game;
            game.begin(scene);
            CHECK(!game.getStatus().hasPlayer);
            game.update(scene, forward, 0.1f);
        }

        // The goal stays shut until every pickup is in, and pickups do not block the way.
        {
            GameLevel level;
            Shape* pickup = level.addRole(GameRole::Pickup, glm::vec3(0.0f, 1.0f, -3.0f));
            Shape* goal = level.addRole(GameRole::Goal, glm::vec3(0.0f, 1.0f, -8.0f));
            level.addRole(GameRole::Pickup, glm::vec3(3.0f, 1.0f, 0.0f));

            GameSession game;
            game.begin(level.scene);
            CHECK(game.getStatus().hasPlayer);
            CHECK(game.getStatus().total == 2);

            level.run(game, GameInput(), 0.5f);
            const float grounded = level.player->position.y;
            CHECK(grounded > 0.9f && grounded < 1.2f);

            // Walks through the first pickup and up to the goal, which stays locked.
            level.run(game, forward, 1.2f);
            CHECK(game.getStatus().collected == 1);
            CHECK(!pickup->visible);
            CHECK(game.getStatus().goalLocked);
            CHECK(!game.getStatus().won);

            // A second touch does not count twice, and the other pickup opens the goal.
            GameInput right;
            right.moveRight = 1.0f;
            // Teleports keep no momentum from the walk before.
            level.player->setPosition(glm::vec3(0.0f, 1.1f, 0.0f));
            level.player->velocity = glm::vec3(0.0f);
            level.run(game, right, 1.0f);
            CHECK(game.getStatus().collected == 2);

            level.player->setPosition(glm::vec3(0.0f, 1.1f, -6.0f));
            level.player->velocity = glm::vec3(0.0f);
            level.run(game, forward, 0.5f);
            CHECK(game.getStatus().won);

            // A finished run freezes the player and the clock.
            const glm::vec3 finished = level.player->position;
            const float time = game.getStatus().seconds;
            level.run(game, forward, 0.5f);
            CHECK(level.player->position == finished);
            CHECK(game.getStatus().seconds == time);
            (void)goal;
        }

        // A goal alone is reachable at once: zero pickups means nothing to wait for.
        {
            GameLevel level;
            level.addRole(GameRole::Goal, glm::vec3(0.0f, 1.0f, -3.0f));
            GameSession game;
            game.begin(level.scene);
            level.run(game, forward, 1.0f);
            CHECK(game.getStatus().won);
        }

        // Hazards and long falls send the player back, keeping what was collected.
        {
            GameLevel level;
            level.addRole(GameRole::Pickup, glm::vec3(0.0f, 1.0f, -2.0f));
            level.addRole(GameRole::Hazard, glm::vec3(0.0f, 1.0f, -6.0f));

            GameSession game;
            game.begin(level.scene);
            level.run(game, forward, 1.2f);
            CHECK(game.getStatus().falls == 1);
            CHECK(game.getStatus().collected == 1);
            CHECK(level.player->position.z > -3.0f);

            GameInput right;
            right.moveRight = 1.0f;
            level.run(game, right, 2.2f);
            CHECK(game.getStatus().falls >= 2);
            CHECK(std::abs(level.player->position.x) < 4.0f);
        }

        // Jumping needs ground, and clears about 1.5 units.
        {
            GameLevel level;
            GameSession game;
            game.begin(level.scene);
            level.run(game, GameInput(), 0.5f);
            const float ground = level.player->position.y;

            GameInput jump;
            jump.jump = true;
            float highest = ground;
            for (int i = 0; i < 60; i++) {
                level.run(game, i < 6 ? jump : GameInput(), 1.0f / 120.0f);
                highest = std::max(highest, level.player->position.y);
            }
            CHECK(highest > ground + 1.0f);
            CHECK(highest < ground + 2.0f);

            // Walking off a ledge leaves a moment to jump, but not a jump in mid-air.
            GameLevel ledge;
            GameSession ledgeGame;
            ledgeGame.begin(ledge.scene);
            ledge.player->setPosition(glm::vec3(0.0f, 4.0f, 0.0f));
            ledge.run(ledgeGame, GameInput(), 0.2f);
            const float falling = ledge.player->velocity.y;
            CHECK(falling < 0.0f);
            ledge.run(ledgeGame, jump, 1.0f / 120.0f);
            CHECK(ledge.player->velocity.y < 0.0f);
        }

        // A patrol swings around its start and stays within its range.
        {
            GameLevel level;
            Shape* hazard = level.addRole(GameRole::Hazard, glm::vec3(20.0f, 1.0f, 0.0f));
            hazard->patrolOffset = glm::vec3(0.0f, 0.0f, 4.0f);
            hazard->patrolSeconds = 2.0f;

            GameSession game;
            game.begin(level.scene);
            level.run(game, GameInput(), 0.5f);
            CHECK(std::abs(hazard->position.z - 4.0f) < 0.1f);
            level.run(game, GameInput(), 1.0f);
            CHECK(std::abs(hazard->position.z + 4.0f) < 0.1f);
            CHECK(hazard->position.x == 20.0f);
        }

        // The camera trails the player, and moving is relative to where it looks.
        {
            GameLevel level;
            GameSession game;
            game.begin(level.scene);
            level.run(game, GameInput(), 0.2f);

            CameraObject* camera = level.scene.getMainCamera();
            CHECK(camera->getWorldPosition().z > level.player->position.z + 5.0f);
            CHECK(camera->getWorldPosition().y > level.player->position.y + 2.0f);

            GameInput turned = forward;
            turned.turnDegrees = 90.0f;
            game.update(level.scene, turned, 1.0f / 120.0f);
            level.run(game, forward, 0.5f);
            CHECK(level.player->position.x > 1.0f);
            CHECK(std::abs(level.player->position.z) < 0.5f);
        }

        // First person: the camera is at the eyes, the body only casts a shadow, and looking
        // decides where walking goes, without tilting into the ground.
        {
            GameLevel level;
            level.scene.getMainCamera()->firstPerson = true;
            GameSession game;
            game.begin(level.scene);
            CHECK(game.getStatus().firstPerson);
            CHECK(level.player->shadowOnly);
            level.run(game, GameInput(), 0.5f);

            CameraObject* camera = level.scene.getMainCamera();
            CHECK(glm::length(camera->getWorldPosition() - level.player->getCameraPosition()) < 0.05f);

            GameInput look;
            look.turnDegrees = 90.0f;
            look.lookUpDegrees = -200.0f;
            game.update(level.scene, look, 1.0f / 120.0f);
            const glm::vec3 lookDirection = -glm::normalize(glm::vec3(camera->getWorldMatrix()[2]));
            CHECK(lookDirection.y < -0.98f);
            CHECK(lookDirection.y > -1.0f);

            level.run(game, forward, 0.5f);
            CHECK(level.player->position.x > 2.0f);
            CHECK(std::abs(level.player->position.z) < 0.1f);

            game.setFirstPerson(level.scene, false);
            CHECK(!level.player->shadowOnly);
            CHECK(glm::length(camera->getWorldPosition() - level.player->getCameraPosition()) > 5.0f);
        }

        // Physics leaves the player to its own movement.
        {
            GameLevel level;
            level.player->velocity = glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 before = level.player->position;
            level.scene.update(0.1f);
            CHECK(level.player->position == before);
        }
    }

    void testWorldGame(AssetManager& assets, const fs::path& root) {
        {
            std::ofstream file(root / "Assets/Models/hill.obj");
            // A 40 x 40 slope rising towards +X, from height 0 to 4.
            file << "v -20 0 -20\nv 20 4 -20\nv 20 4 20\nv -20 0 20\nf 1 4 3 2\n";
        }

        auto makeWorld = [&](SceneGraph& scene) {
            auto groundObject = std::make_shared<MeshShape>();
            groundObject->setMesh(assets.getMesh("Assets/Models/hill.obj"), "Assets/Models/hill.obj");
            groundObject->gameRole = GameRole::Ground;
            scene.add(groundObject);

            auto player = ObjectFactory::create(ObjectType::Player);

            static_cast<Player*>(player.get())->gameControls = true;
            player->setPosition(glm::vec3(0.0f, 5.0f, 0.0f));
            scene.add(player);
            scene.add(ObjectFactory::create(ObjectType::Camera));

            for (GameRole role : {GameRole::Sword, GameRole::Bow, GameRole::Arrow}) {
                auto item = ObjectFactory::create(ObjectType::Cube);
                static_cast<Shape*>(scene.add(item))->gameRole = role;
            }
        };

        auto addActor = [&](SceneGraph& scene, GameRole role, glm::vec3 position) {
            auto object = ObjectFactory::create(ObjectType::Cube);
            object->setPosition(position);
            object->setScale(glm::vec3(1.0f));
            Shape* shape = static_cast<Shape*>(scene.add(object));
            shape->gameRole = role;
            shape->isStatic = true;
            return shape;
        };

        auto run = [](SceneGraph& scene, GameSession& game, const GameInput& input, float seconds) {
            for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f) {
                game.update(scene, input, 1.0f / 60.0f);
                scene.update(1.0f / 60.0f);
            }
        };

        // Walking follows the slope, both up and down.
        {
            SceneGraph scene;
            makeWorld(scene);
            GameSession game;
            game.begin(scene);
            CHECK(game.getStatus().hasCombat);
            run(scene, game, GameInput(), 1.0f);
            Player* player = scene.getPlayer();
            CHECK(std::abs(player->position.y - (2.0f + player->scale.y * 0.5f)) < 0.2f);

            GameInput right;
            right.moveRight = 1.0f;
            run(scene, game, right, 1.0f);
            CHECK(std::abs(player->position.y - (2.0f + player->position.x * 0.1f + player->scale.y * 0.5f)) < 0.2f);
            GameInput left;
            left.moveRight = -1.0f;
            run(scene, game, left, 2.0f);
            CHECK(std::abs(player->position.y - (2.0f + player->position.x * 0.1f + player->scale.y * 0.5f)) < 0.5f);

            // The world's edge holds.
            run(scene, game, left, 6.0f);
            CHECK(player->position.x > -20.0f);
        }

        // A bandit notices, closes in and hurts; two sword hits are not enough, three are.
        {
            SceneGraph scene;
            makeWorld(scene);
            Shape* bandit = addActor(scene, GameRole::Bandit, glm::vec3(0.0f, 2.0f, -9.0f));
            GameSession game;
            game.begin(scene);
            CHECK(game.getStatus().banditsTotal == 1);

            run(scene, game, GameInput(), 3.5f);
            CHECK(glm::length(glm::vec2(bandit->position.x, bandit->position.z)) < 3.0f);
            CHECK(game.getStatus().health < 100.0f);
            CHECK(!game.getStatus().won);

            GameInput attack;
            attack.attack = true;
            run(scene, game, attack, 1.8f);
            CHECK(game.getStatus().banditsLeft == 0);
            CHECK(game.getStatus().won);
        }

        // The bow reaches what the sword cannot, in the direction of the look.
        {
            SceneGraph scene;
            makeWorld(scene);
            addActor(scene, GameRole::Bandit, glm::vec3(0.0f, 2.0f, -14.0f));
            GameSession game;
            game.begin(scene);
            run(scene, game, GameInput(), 0.5f);

            GameInput shoot;
            shoot.number = 2;
            shoot.attack = true;
            run(scene, game, shoot, 2.6f);
            CHECK(game.getStatus().weapon == 2);
            CHECK(game.getStatus().banditsLeft == 0);
        }

        // Villagers wander, stay near home and run when a fight breaks out nearby.
        {
            SceneGraph scene;
            makeWorld(scene);
            Shape* villager = addActor(scene, GameRole::Villager, glm::vec3(10.0f, 3.0f, 0.0f));
            GameSession game;
            game.begin(scene);
            run(scene, game, GameInput(), 12.0f);
            CHECK(glm::length(villager->position - glm::vec3(10.0f, 3.0f, 0.0f)) > 0.3f);
            CHECK(glm::length(villager->position - glm::vec3(10.0f, 3.0f, 0.0f)) < 13.0f);
            CHECK(std::abs(villager->position.y - (2.0f + villager->position.x * 0.1f)) < 0.2f);
        }

        // A horse is mounted nearby, is much faster, and is left where it stops.
        {
            SceneGraph scene;
            makeWorld(scene);
            Shape* horse = addActor(scene, GameRole::Horse, glm::vec3(2.0f, 2.0f, 0.0f));
            GameSession game;
            game.begin(scene);
            run(scene, game, GameInput(), 0.5f);
            CHECK(game.getStatus().prompt == "Ride");

            GameInput use;
            use.interact = true;
            game.update(scene, use, 1.0f / 60.0f);
            CHECK(game.getStatus().mounted);

            GameInput forward;
            forward.moveForward = 1.0f;
            const float start = scene.getPlayer()->position.z;
            run(scene, game, forward, 1.0f);
            CHECK(start - scene.getPlayer()->position.z > 11.0f);
            CHECK(std::abs(horse->position.z - scene.getPlayer()->position.z) < 0.01f);

            game.update(scene, use, 1.0f / 60.0f);
            CHECK(!game.getStatus().mounted);
            const float parked = horse->position.z;
            run(scene, game, forward, 0.3f);
            CHECK(horse->position.z == parked);
        }

        // Losing all health returns the player to the start with full health.
        {
            SceneGraph scene;
            makeWorld(scene);
            addActor(scene, GameRole::Bandit, glm::vec3(2.0f, 2.0f, 0.0f));
            GameSession game;
            game.begin(scene);
            for (int i = 0; i < 20 * 60 && game.getStatus().falls == 0; i++) {
                run(scene, game, GameInput(), 1.0f / 60.0f);
            }
            CHECK(game.getStatus().falls == 1);
            CHECK(game.getStatus().health == 100.0f);
        }

        // Fog settings survive saving, and fog changes the image.
        {
            SceneGraph scene;
            scene.fogEnd = 50.0f;
            SceneGraph loaded;
            CHECK(SceneSerializer::deserialize(SceneSerializer::serialize(scene), loaded, assets));
            CHECK(loaded.fogEnd == 50.0f);

            RenderFixture fixture(assets);
            const Image clear = fixture.render(RenderSettings());
            fixture.scene.fogStart = 1.0f;
            fixture.scene.fogEnd = 14.0f;
            fixture.scene.clearColor = glm::vec3(0.6f, 0.2f, 0.8f);
            CHECK(differingPixels(clear, fixture.render(RenderSettings())) > 1000);
        }
    }

    // ---- Open world -----------------------------------------------------------------------

    void writeObj(const fs::path& path, const std::string& text) {
        fs::create_directories(path.parent_path());
        std::ofstream(path) << text;
    }

    // A 4x4 texture: an opaque left half and a transparent right half.
    void writeCutoutTexture(const fs::path& path) {
        std::ofstream file(path, std::ios::binary);
        unsigned char header[18] = {};
        header[2] = 2;
        header[12] = 4;
        header[14] = 4;
        header[16] = 32;
        header[17] = 8;
        file.write(reinterpret_cast<const char*>(header), sizeof(header));
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                const unsigned char pixel[4] = {40, 200, 40, (unsigned char)(x < 2 ? 255 : 0)};
                file.write(reinterpret_cast<const char*>(pixel), 4);
            }
        }
    }

    struct World {
        SceneGraph scene;
        Player* player = nullptr;
        GameSession game;

        // flat ground at height 0, or a mesh given by asset path
        World(AssetManager& assets, const std::string& groundAsset = "Assets/Models/flat.obj", bool combat = true) {
            auto ground = std::make_shared<MeshShape>();
            ground->setMesh(assets.getMesh(groundAsset), groundAsset);
            ground->gameRole = GameRole::Ground;
            scene.add(ground);

            auto playerObject = ObjectFactory::create(ObjectType::Player);

            static_cast<Player*>(playerObject.get())->gameControls = true;
            playerObject->setPosition(glm::vec3(0.0f, 1.0f, 0.0f));
            player = static_cast<Player*>(scene.add(playerObject));
            scene.add(ObjectFactory::create(ObjectType::Camera));

            if (combat) {
                for (GameRole role : {GameRole::Sword, GameRole::Bow, GameRole::Arrow, GameRole::Arrow}) {
                    static_cast<Shape*>(scene.add(ObjectFactory::create(ObjectType::Cube)))->gameRole = role;
                }
            }
        }

        Shape* box(glm::vec3 position, glm::vec3 size, bool dynamic = false) {
            auto object = ObjectFactory::create(ObjectType::Cube);
            object->setPosition(position);
            object->setScale(size);
            Shape* shape = static_cast<Shape*>(scene.add(object));
            shape->isStatic = !dynamic;
            shape->useGravity = dynamic;
            return shape;
        }

        Shape* role(GameRole gameRole, glm::vec3 position, const std::string& name = "", const std::string& parameter = "") {
            Shape* shape = box(position, glm::vec3(1.0f));
            shape->gameRole = gameRole;
            if (!name.empty()) shape->name = name;
            shape->gameParameter = parameter;
            return shape;
        }

        void run(const GameInput& input, float seconds) {
            for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f) {
                game.update(scene, input, 1.0f / 60.0f);
                scene.update(1.0f / 60.0f);
            }
        }

        void press(GameInput input) {
            game.update(scene, input, 1.0f / 60.0f);
            scene.update(1.0f / 60.0f);
        }
    };

    GameInput moving(float forward, float right) {
        GameInput input;
        input.moveForward = forward;
        input.moveRight = right;
        return input;
    }

    // Particles, fire and water are objects of their own, drawn on top of the scene.
    void testEffects(AssetManager& assets) {
        RenderFixture fixture(assets);
        const RenderSettings settings;
        const Image reference = fixture.render(settings);
        while (glGetError() != GL_NO_ERROR) {}

        // A stream of particles: nothing before it has run, something while it does, and nothing
        // again once it is stopped and the last have died.
        {
            auto* emitter = static_cast<ParticleEmitter*>(fixture.scene.add(ObjectFactory::create(ObjectType::Particles)));
            emitter->setPosition(glm::vec3(0.0f, 3.0f, 0.0f));
            ParticleSettings& p = emitter->system.settings;
            p.rate = 200.0f;
            p.gravity = glm::vec3(0.0f);
            p.sizeStart = p.sizeEnd = 0.5f;
            p.colorStart = p.colorMid = p.colorEnd = glm::vec4(1.0f, 0.1f, 0.1f, 1.0f);
            p.additive = false;
            p.speedMin = p.speedMax = 0.2f;
            p.lifetimeMin = p.lifetimeMax = 1.5f;

            CHECK(differingPixels(reference, fixture.render(settings)) == 0);
            for (int i = 0; i < 30; i++) fixture.scene.animate(0.05f);
            CHECK(emitter->system.count() > 100);
            CHECK(differingPixels(reference, fixture.render(settings)) > 100);
            CHECK(glGetError() == GL_NO_ERROR);

            emitter->visible = false;
            CHECK(differingPixels(reference, fixture.render(settings)) == 0);
            emitter->visible = true;

            emitter->system.playing = false;
            for (int i = 0; i < 40; i++) fixture.scene.animate(0.05f);
            CHECK(emitter->system.count() == 0);
            CHECK(differingPixels(reference, fixture.render(settings)) == 0);
            fixture.scene.remove(emitter->id);
            CHECK(fixture.scene.getEmitters().empty());
        }

        // A fire lights what is near it, flickers, and goes out.
        {
            auto* fire = static_cast<FireObject*>(fixture.scene.add(ObjectFactory::create(ObjectType::Fire)));
            fire->setPosition(glm::vec3(0.0f, 0.1f, 3.0f));
            for (int i = 0; i < 40; i++) fixture.scene.animate(0.05f);
            CHECK(fire->flames.count() > 20 && fire->smokeCloud.count() > 0 && fire->sparkShower.count() > 0);
            CHECK(differingPixels(reference, fixture.render(settings)) > 200);
            CHECK(glGetError() == GL_NO_ERROR);

            glm::vec4 positions[8];
            glm::vec3 colors[8];
            CHECK(fixture.scene.getPointLights(glm::vec3(0.0f), positions, colors, 8) == 1);
            float lowest = 1e9f, highest = 0.0f;
            for (int i = 0; i < 100; i++) {
                const float level = fire->lightLevel(float(i) * 0.05f);
                lowest = std::min(lowest, level);
                highest = std::max(highest, level);
            }
            CHECK(highest - lowest > 0.15f);

            fire->burning = false;
            for (int i = 0; i < 100; i++) fixture.scene.animate(0.05f);
            CHECK(fire->flames.count() == 0 && fire->smokeCloud.count() == 0 && fire->sparkShower.count() == 0);
            CHECK(fixture.scene.getPointLights(glm::vec3(0.0f), positions, colors, 8) == 0);
            CHECK(differingPixels(reference, fixture.render(settings)) == 0);
            fixture.scene.remove(fire->id);
        }

        // Water: a surface, with waves that move, and a body one can be under.
        {
            auto* water = static_cast<WaterObject*>(fixture.scene.add(ObjectFactory::create(ObjectType::Water)));
            water->setPosition(glm::vec3(0.0f, 0.4f, 0.0f));
            water->size = glm::vec2(8.0f);
            const Image still = fixture.render(settings);
            CHECK(differingPixels(reference, still) > 500);
            CHECK(glGetError() == GL_NO_ERROR);

            fixture.scene.animate(0.7f);
            CHECK(differingPixels(still, fixture.render(settings)) > 20);

            water->round = true;
            CHECK(water->contains(glm::vec2(0.0f)) && !water->contains(glm::vec2(3.9f, 3.9f)));
            water->round = false;
            CHECK(water->contains(glm::vec2(3.9f, 3.9f)) && !water->contains(glm::vec2(4.1f, 0.0f)));

            // From above it is not under water; from below it is.
            CHECK(!fixture.scene.getUnderwater(glm::vec3(0.0f, 5.0f, 0.0f)).active);
            CHECK(!fixture.scene.getUnderwater(glm::vec3(20.0f, 0.0f, 0.0f)).active);
            const UnderwaterView below = fixture.scene.getUnderwater(glm::vec3(0.0f, -1.0f, 0.0f));
            CHECK(below.active && std::abs(below.depth - 1.4f) < 0.01f);

            // A camera under a wide surface sees the world turn blue-green.
            fixture.scene.remove(water->id);
            const Image dry = fixture.render(settings, glm::vec3(5.0f, 2.0f, 9.0f));
            water = static_cast<WaterObject*>(fixture.scene.add(ObjectFactory::create(ObjectType::Water)));
            water->setPosition(glm::vec3(0.0f, 6.0f, 0.0f));
            water->size = glm::vec2(60.0f);
            const Image wet = fixture.render(settings, glm::vec3(5.0f, 2.0f, 9.0f));
            CHECK(differingPixels(dry, wet) > 5000);
            long red = 0, blue = 0;
            for (size_t i = 0; i < wet.size(); i += 4) {
                red += wet[i];
                blue += wet[i + 2];
            }
            CHECK(blue > red);
            CHECK(glGetError() == GL_NO_ERROR);
        }

        // Effects keep what they are made of through a save.
        {
            SceneGraph scene;
            auto* emitter = static_cast<ParticleEmitter*>(scene.add(ObjectFactory::create(ObjectType::Particles)));
            emitter->system.settings.rate = 12.5f;
            emitter->system.settings.coneDegrees = 40.0f;
            emitter->system.settings.colorMid = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
            emitter->system.settings.additive = false;
            emitter->system.playing = false;
            auto* fire = static_cast<FireObject*>(scene.add(ObjectFactory::create(ObjectType::Fire)));
            fire->size = 2.5f;
            fire->lightRange = 21.0f;
            fire->burning = false;
            auto* water = static_cast<WaterObject*>(scene.add(ObjectFactory::create(ObjectType::Water)));
            water->size = glm::vec2(12.0f, 7.0f);
            water->round = true;
            water->flow = glm::vec2(0.5f, -1.0f);
            water->opacity = 0.4f;
            auto* sound = static_cast<AudioSourceObject*>(scene.add(ObjectFactory::create(ObjectType::AudioSource)));
            sound->clip = "Assets/Audio/outdoor/river.wav";
            sound->volume = 0.6f;
            sound->pitch = 1.5f;
            sound->loop = false;
            sound->spatial = true;
            sound->nearDistance = 4.0f;
            sound->farDistance = 55.0f;
            sound->repeatMin = 3.0f;
            sound->repeatMax = 9.0f;
            sound->muted = true;
            auto* element = static_cast<UiObject*>(scene.add(ObjectFactory::create(ObjectType::Ui)));
            element->kind = UiKind::Bar;
            element->anchor = UiAnchor::BottomRight;
            element->offset = glm::vec2(-20.0f, -12.0f);
            element->size = glm::vec2(180.0f, 10.0f);
            element->color = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
            element->binding = UiBinding::Stamina;
            element->text = "Stamina";
            element->stretch = true;
            auto* newPlayer = static_cast<Player*>(scene.add(ObjectFactory::create(ObjectType::Player)));
            CHECK(!newPlayer->gameControls);   // a new player has no game rules of its own

            SceneGraph loaded;
            std::string error;
            CHECK(SceneSerializer::deserialize(SceneSerializer::serialize(scene), loaded, assets, &error));
            CHECK(loaded.getEmitters().size() == 1 && loaded.getFires().size() == 1 && loaded.getWaters().size() == 1);
            if (loaded.getEmitters().size() == 1 && loaded.getFires().size() == 1 && loaded.getWaters().size() == 1) {
                const ParticleSettings& p = loaded.getEmitters()[0]->system.settings;
                CHECK(p.rate == 12.5f && p.coneDegrees == 40.0f && !p.additive && p.colorMid.w == 0.4f);
                CHECK(!loaded.getEmitters()[0]->system.playing);
                CHECK(loaded.getFires()[0]->size == 2.5f && loaded.getFires()[0]->lightRange == 21.0f && !loaded.getFires()[0]->burning);
                const WaterObject& w = *loaded.getWaters()[0];
                CHECK(w.size == glm::vec2(12.0f, 7.0f) && w.round && w.flow == glm::vec2(0.5f, -1.0f) && w.opacity == 0.4f);
            }
            const UiObject* loadedUi = nullptr;
            for (const auto& o : loaded.getObjects()) {
                if (o->getType() == ObjectType::Ui) loadedUi = static_cast<const UiObject*>(o.get());
            }
            CHECK(loadedUi != nullptr);
            if (loadedUi) {
                CHECK(loadedUi->kind == UiKind::Bar && loadedUi->anchor == UiAnchor::BottomRight && loadedUi->stretch);
                CHECK(loadedUi->offset == glm::vec2(-20.0f, -12.0f) && loadedUi->size == glm::vec2(180.0f, 10.0f));
                CHECK(loadedUi->color == glm::vec4(0.1f, 0.2f, 0.3f, 0.4f) && loadedUi->binding == UiBinding::Stamina && loadedUi->text == "Stamina");
            }
            CHECK(loaded.getPlayer() && !loaded.getPlayer()->gameControls);
            CHECK(loaded.getAudioSources().size() == 1);
            if (loaded.getAudioSources().size() == 1) {
                const AudioSourceObject& a = *loaded.getAudioSources()[0];
                CHECK(a.clip == "Assets/Audio/outdoor/river.wav" && a.volume == 0.6f && a.pitch == 1.5f && !a.loop && a.muted);
                CHECK(a.nearDistance == 4.0f && a.farDistance == 55.0f && a.repeatMin == 3.0f && a.repeatMax == 9.0f);
            }
        }
    }

    // A point light's shadow comes from rays traced against the still geometry, and only when asked for.
    void testRayTracedShadows() {
        SceneGraph scene;
        scene.ambientColor = glm::vec3(0.05f);
        Shape* floor = addShape(scene, ObjectType::Plane, glm::vec3(0.0f), glm::vec3(20.0f, 1.0f, 20.0f), glm::vec3(0.8f));
        floor->isStatic = true;
        Shape* block = addShape(scene, ObjectType::Cube, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(2.0f), glm::vec3(0.8f));
        block->isStatic = true;
        auto* lamp = static_cast<LightObject*>(scene.add(ObjectFactory::create(ObjectType::Light)));
        lamp->setPosition(glm::vec3(3.0f, 5.0f, 0.0f));
        lamp->type = LightType::Point;
        lamp->range = 30.0f;
        lamp->intensity = 3.0f;
        lamp->radius = 0.5f;

        CHECK(scene.getRayTraceData() && scene.getRayTraceData()->triangleCount() == 2 + 12);

        Renderer renderer(512);
        RenderTarget target;
        target.resize(WIDTH, HEIGHT);
        auto frame = [&](bool traced) {
            RenderSettings settings;
            settings.rayTracedShadows = traced;
            renderer.render(scene, makeCamera(glm::vec3(-6.0f, 7.0f, 7.0f)), RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, settings);
            return readImage(target.framebuffer());
        };
        const Image open = frame(false);
        const Image traced = frame(true);
        CHECK(differingPixels(open, traced) > 800);

        // The shadow is darker, never brighter.
        double before = 0.0, after = 0.0;
        for (size_t i = 0; i < open.size(); i += 4) {
            before += open[i] + open[i + 1] + open[i + 2];
            after += traced[i] + traced[i + 1] + traced[i + 2];
        }
        CHECK(after < before * 0.98);

        // Switching the light's shadows off takes the rays away.
        lamp->castShadows = false;
        CHECK(differingPixels(open, frame(true)) < differingPixels(open, traced) / 4);

        // A scene that moves a still object tells the tracer.
        lamp->castShadows = true;
        block->setPosition(glm::vec3(0.0f, 1.0f, 40.0f));
        scene.invalidateRayTracing();
        CHECK(differingPixels(frame(false), frame(true)) < differingPixels(open, traced) / 4);
    }

    // A terrain is shaped by brushes, found by a ray, painted by tile, saved, and meshed.
    void testTerrain(AssetManager& assets) {
        TerrainObject terrain;
        terrain.reset(16, 1.0f);
        CHECK(terrain.heights.size() == 17 * 17 && terrain.tiles.size() == 256 && terrain.size() == 16.0f);
        CHECK(terrain.contains(0.0f, 0.0f) && !terrain.contains(9.0f, 0.0f));

        // Raising lifts the middle of the brush and leaves what is outside it.
        for (int i = 0; i < 20; i++) terrain.applyBrush(TerrainObject::Brush::Raise, glm::vec2(0.0f), 3.0f, 4.0f, 0.5f, 0.05f);
        CHECK(terrain.heightAt(0.0f, 0.0f) > 3.0f);
        CHECK(terrain.heightAt(6.5f, 6.5f) == 0.0f);
        const float peak = terrain.heightAt(0.0f, 0.0f);

        // Smoothing lowers a peak and keeps the ground continuous; lowering undoes raising.
        for (int i = 0; i < 20; i++) terrain.applyBrush(TerrainObject::Brush::Smooth, glm::vec2(0.0f), 4.0f, 1.0f, 0.5f, 0.05f);
        CHECK(terrain.heightAt(0.0f, 0.0f) < peak);
        TerrainObject flat;
        flat.reset(16, 1.0f);
        for (int i = 0; i < 20; i++) flat.applyBrush(TerrainObject::Brush::Raise, glm::vec2(0.0f), 3.0f, 4.0f, 0.5f, 0.05f);
        for (int i = 0; i < 20; i++) flat.applyBrush(TerrainObject::Brush::Lower, glm::vec2(0.0f), 3.0f, 4.0f, 0.5f, 0.05f);
        CHECK(std::abs(flat.heightAt(0.0f, 0.0f)) < 0.001f);
        for (int i = 0; i < 40; i++) terrain.applyBrush(TerrainObject::Brush::Flatten, glm::vec2(0.0f), 5.0f, 1.0f, 0.2f, 0.05f, 1.0f);
        CHECK(std::abs(terrain.heightAt(0.0f, 0.0f) - 1.0f) < 0.05f);

        // A ray from above meets the surface where the height is.
        glm::vec3 hit;
        CHECK(terrain.raycast(glm::vec3(0.3f, 20.0f, -0.2f), glm::vec3(0.0f, -1.0f, 0.0f), hit));
        CHECK(std::abs(hit.y - terrain.heightAt(0.3f, -0.2f)) < 0.05f);
        CHECK(!terrain.raycast(glm::vec3(30.0f, 20.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), hit));

        // Painting changes the tiles inside the brush only.
        terrain.atlasColumns = 4;
        terrain.atlasRows = 2;
        CHECK(terrain.applyBrush(TerrainObject::Brush::Paint, glm::vec2(0.0f), 2.0f, 1.0f, 0.0f, 0.016f, 0.0f, 5));
        CHECK(terrain.tiles[8 * 16 + 8] == 5 && terrain.tiles[0] == 0);

        // The mesh has two triangles per cell and its UVs follow the tile.
        terrain.rebuild();
        CHECK(terrain.getMesh() && terrain.getMesh()->getTriangles().size() == 16 * 16 * 6);
        CHECK(!terrain.needsRebuild());

        // It saves, and comes back as it was.
        SceneGraph scene;
        auto* original = static_cast<TerrainObject*>(scene.add(ObjectFactory::create(ObjectType::Terrain)));
        original->reset(8, 2.0f);
        original->generate(7, 3.0f, 10.0f, 3);
        original->atlasColumns = 4;
        original->atlasRows = 2;
        original->tiles[10] = 6;
        SceneGraph loaded;
        std::string error;
        CHECK(SceneSerializer::deserialize(SceneSerializer::serialize(scene), loaded, assets, &error));
        const TerrainObject* copy = nullptr;
        for (const auto& shape : loaded.getShapes()) {
            if (shape->getType() == ObjectType::Terrain) copy = static_cast<const TerrainObject*>(shape.get());
        }
        CHECK(copy != nullptr);
        if (copy) {
            CHECK(copy->cells == 8 && copy->cellSize == 2.0f && copy->atlasColumns == 4 && copy->tiles[10] == 6);
            CHECK(copy->heights.size() == original->heights.size());
            float worst = 0.0f;
            for (size_t i = 0; i < copy->heights.size(); i++) worst = std::max(worst, std::abs(copy->heights[i] - original->heights[i]));
            CHECK(worst < 0.001f);
            CHECK(copy->gameRole == GameRole::Ground);
        }
    }

    // Swimming on the surface, diving, and running out of air.
    void testDiving(AssetManager& assets) {
        World world(assets);
        auto* water = static_cast<WaterObject*>(world.scene.add(ObjectFactory::create(ObjectType::Water)));
        water->setPosition(glm::vec3(0.0f, 6.0f, 0.0f));
        water->size = glm::vec2(60.0f);
        world.game.begin(world.scene);

        // On the surface the head is out, and there is air to spare.
        world.run(GameInput(), 3.0f);
        CHECK(world.game.getStatus().swimming && !world.game.getStatus().diving && !world.game.getStatus().underwater);
        const float floating = world.player->position.y;
        CHECK(floating > 4.5f);

        GameInput dive;
        dive.dive = true;
        world.run(dive, 1.0f);
        CHECK(world.game.getStatus().diving && world.game.getStatus().underwater);
        CHECK(world.player->position.y < floating - 1.0f);
        CHECK(world.game.getStatus().breath < 20.0f);

        // A diver stays down without being pushed, and the camera goes under with them.
        world.run(GameInput(), 1.0f);
        CHECK(world.game.getStatus().diving);
        CHECK(world.scene.getUnderwater(world.scene.getMainCamera()->getWorldPosition()).active);

        GameInput rise;
        rise.jump = true;
        world.run(rise, 3.0f);
        CHECK(!world.game.getStatus().diving && !world.game.getStatus().underwater);
        world.run(GameInput(), 6.0f);
        CHECK(world.game.getStatus().breath > 19.0f);

        // Without air a diver is hurt.
        world.game.begin(world.scene);
        world.player->setPosition(glm::vec3(0.0f, 1.0f, 0.0f));
        world.run(dive, 22.0f);
        CHECK(world.game.getStatus().breath == 0.0f);
        CHECK(world.game.getStatus().health < world.game.getStatus().maxHealth);

        // A pond is as wide as its outline, not its rectangle.
        water->round = true;
        water->size = glm::vec2(10.0f);
        water->setPosition(glm::vec3(30.0f, 6.0f, 0.0f));
        world.game.begin(world.scene);
        world.player->setPosition(glm::vec3(34.0f, 1.0f, 3.5f));   // in the corner of the rectangle
        world.run(GameInput(), 1.0f);
        CHECK(!world.game.getStatus().swimming);
        world.player->setPosition(glm::vec3(30.0f, 1.0f, 0.0f));
        world.run(GameInput(), 1.0f);
        CHECK(world.game.getStatus().swimming);
    }

    void testFireDamage(AssetManager& assets) {
        World world(assets);
        auto* fire = static_cast<FireObject*>(world.scene.add(ObjectFactory::create(ObjectType::Fire)));
        fire->setPosition(glm::vec3(20.0f, 0.0f, 0.0f));
        world.game.begin(world.scene);

        // Away from the flames nothing happens.
        world.player->setPosition(glm::vec3(5.0f, 1.0f, 0.0f));
        world.run(GameInput(), 2.0f);
        CHECK(!world.game.getStatus().burning);
        CHECK(world.game.getStatus().health == world.game.getStatus().maxHealth);

        // Standing in them hurts, and the harm follows the fire's damage.
        world.player->setPosition(glm::vec3(20.0f, 1.0f, 0.0f));
        world.run(GameInput(), 1.0f);
        CHECK(world.game.getStatus().burning);
        const float hurt = world.game.getStatus().maxHealth - world.game.getStatus().health;
        CHECK(hurt > 4.0f && hurt < 12.0f);

        // A fire that is out, or harmless, does not.
        world.game.begin(world.scene);
        fire->burning = false;
        world.player->setPosition(glm::vec3(20.0f, 1.0f, 0.0f));
        world.run(GameInput(), 2.0f);
        CHECK(!world.game.getStatus().burning && world.game.getStatus().health == world.game.getStatus().maxHealth);
        fire->burning = true;
        fire->damage = 0.0f;
        world.run(GameInput(), 2.0f);
        CHECK(world.game.getStatus().health == world.game.getStatus().maxHealth);
    }

    void testCharacterController(AssetManager& assets, const fs::path& root) {
        writeObj(root / "Assets/Models/flat.obj", "v -40 0 -40\nv 40 0 -40\nv 40 0 40\nv -40 0 40\nf 1 4 3 2\n");
        writeObj(root / "Assets/Models/cliff.obj", "v -40 0 -40\nv 0 0 -40\nv 0 0 40\nv -40 0 40\nf 1 4 3 2\n"
                                                   "v 0 0 -40\nv 4 8 -40\nv 4 8 40\nv 0 0 40\nf 5 8 7 6\n");

        // Running at a wall at an angle slides along it instead of stopping.
        {
            World world(assets);
            world.box(glm::vec3(2.0f, 1.0f, 0.0f), glm::vec3(1.0f, 2.0f, 40.0f));
            world.game.begin(world.scene);
            world.run(moving(1.0f, 1.0f), 1.5f);
            CHECK(world.player->position.x < 1.1f);
            CHECK(world.player->position.z < -4.0f);
        }

        // A low step is climbed; a box at chest height is not.
        {
            World world(assets);
            world.box(glm::vec3(0.0f, 0.15f, -3.0f), glm::vec3(4.0f, 0.3f, 2.0f));
            world.box(glm::vec3(0.0f, 0.6f, -8.0f), glm::vec3(4.0f, 1.2f, 2.0f));
            world.game.begin(world.scene);
            world.run(GameInput(), 0.3f);
            world.run(moving(1.0f, 0.0f), 0.5f);
            const float half = world.player->scale.y * 0.5f;
            CHECK(world.player->position.z < -2.0f && world.player->position.z > -4.0f);
            CHECK(std::abs(world.player->position.y - (0.3f + half)) < 0.05f);
            world.run(moving(1.0f, 0.0f), 1.5f);
            CHECK(world.player->position.z > -7.0f - 0.45f);
        }

        // Moving speeds up and slows down rather than snapping.
        {
            World world(assets);
            world.game.begin(world.scene);
            world.run(GameInput(), 0.2f);
            world.run(moving(1.0f, 0.0f), 1.0f / 30.0f);
            const float early = -world.player->velocity.z;
            CHECK(early > 0.5f && early < 6.0f);
            world.run(moving(1.0f, 0.0f), 0.5f);
            CHECK(std::abs(-world.player->velocity.z - 6.5f) < 0.1f);

            GameInput sprint = moving(1.0f, 0.0f);
            sprint.sprint = true;
            world.run(sprint, 0.5f);
            CHECK(-world.player->velocity.z > 9.0f);
            CHECK(world.game.getStatus().stamina < 95.0f);
        }

        // A slope too steep to walk holds the player at its foot.
        {
            World world(assets, "Assets/Models/cliff.obj");
            world.player->setPosition(glm::vec3(-3.0f, 1.0f, 0.0f));
            world.game.begin(world.scene);
            world.run(moving(0.0f, 1.0f), 3.0f);
            CHECK(world.player->position.x < 0.3f);
        }

        // A long fall hurts and a short one does not.
        {
            World world(assets);
            world.player->setPosition(glm::vec3(0.0f, 4.0f, 0.0f));
            world.game.begin(world.scene);
            world.run(GameInput(), 1.5f);
            CHECK(world.game.getStatus().health == 100.0f);

            world.player->setPosition(glm::vec3(0.0f, 14.0f, 0.0f));
            world.run(GameInput(), 2.0f);
            CHECK(world.game.getStatus().health < 100.0f);
            CHECK(world.game.getStatus().health > 0.0f);
        }

        // In deep water the player floats with its head out, and swims slower.
        {
            writeObj(root / "Assets/Models/pool.obj", "v -10 0 -10\nv 10 0 -10\nv 10 0 10\nv -10 0 10\nf 1 4 3 2\n");
            writeObj(root / "Assets/Models/basin.obj", "v -40 -4 -40\nv 40 -4 -40\nv 40 -4 40\nv -40 -4 40\nf 1 4 3 2\n");
            World world(assets, "Assets/Models/basin.obj");
            auto water = std::make_shared<MeshShape>();
            water->setMesh(assets.getMesh("Assets/Models/pool.obj"), "Assets/Models/pool.obj");
            water->gameRole = GameRole::Water;
            world.scene.add(water);
            world.player->setPosition(glm::vec3(0.0f, 2.0f, 0.0f));
            world.game.begin(world.scene);
            world.run(GameInput(), 3.0f);
            CHECK(world.game.getStatus().swimming);
            CHECK(world.player->position.y + world.player->scale.y * 0.5f > 0.0f);
            CHECK(world.player->position.y - world.player->scale.y * 0.5f < -0.5f);
            world.run(moving(1.0f, 0.0f), 1.0f);
            CHECK(-world.player->velocity.z < 4.0f);
            CHECK(-world.player->velocity.z > 2.0f);
        }
    }

    void testProps(AssetManager& assets) {
        // A crate falls, lands flush on a box and the box on the ground, and comes to rest.
        {
            World world(assets);
            Shape* table = world.box(glm::vec3(5.0f, 0.5f, 0.0f), glm::vec3(2.0f, 1.0f, 2.0f));
            Shape* crate = world.box(glm::vec3(5.0f, 4.0f, 0.0f), glm::vec3(1.0f), true);
            Shape* loose = world.box(glm::vec3(-5.0f, 3.0f, 0.0f), glm::vec3(1.0f), true);
            (void)table;
            world.game.begin(world.scene);
            world.run(GameInput(), 2.0f);
            CHECK(std::abs(crate->position.y - 1.5f) < 0.01f);
            CHECK(std::abs(loose->position.y - 0.5f) < 0.01f);
            CHECK(glm::length(crate->velocity) < 0.1f);
        }

        // Walking into a crate pushes it, and a sword knocks it away.
        {
            World world(assets);
            Shape* crate = world.box(glm::vec3(0.0f, 0.5f, -2.0f), glm::vec3(1.0f), true);
            world.game.begin(world.scene);
            world.run(GameInput(), 0.3f);
            world.run(moving(1.0f, 0.0f), 1.0f);
            CHECK(crate->position.z < -4.0f);
            CHECK(world.player->position.z > crate->position.z);

            const float before = crate->position.z;
            world.run(GameInput(), 0.6f);
            GameInput attack;
            attack.attack = true;
            world.run(attack, 0.1f);
            world.run(GameInput(), 1.0f);
            CHECK(crate->position.z < before - 0.3f);
        }
    }

    GameDefinition sampleGame() {
        GameDefinition game;
        game.items = {
            {"potion", "Healing Draught", "potion", 5, 40.0f, 0.0f},
            {"arrow", "Arrow", "arrow", 0, 0.0f, 0.0f},
            {"wolf_pelt", "Wolf Pelt", "pelt", 6, 0.0f, 0.0f},
            {"herb", "Moonleaf", "herb", 0, 0.0f, 0.0f},
            {"steel_sword", "Steel Sword", "weapon", 0, 0.0f, 70.0f},
        };

        QuestDefinition report;
        report.id = "report";
        report.title = "Report to the Elder";
        report.summary = "Talk to the Elder";
        report.giver = "Guard";
        report.turnIn = "Elder";
        report.objective = "talk";
        report.target = "Elder";
        report.offer = "See the Elder.";
        report.complete = "Welcome.";
        report.rewardXp = 20;

        QuestDefinition herbs;
        herbs.id = "herbs";
        herbs.title = "Moonleaf";
        herbs.giver = "Elder";
        herbs.after = "report";
        herbs.objective = "collect";
        herbs.target = "herb";
        herbs.count = 2;
        herbs.offer = "Bring two moonleaf.";
        herbs.progress = "Still waiting.";
        herbs.complete = "Thank you.";
        herbs.rewardGold = 30;
        herbs.rewardXp = 90;
        herbs.rewardItems = {{"steel_sword", 1}};

        QuestDefinition wolves;
        wolves.id = "wolves";
        wolves.title = "Wolves";
        wolves.summary = "Defeat wolves";
        wolves.giver = "Hunter";
        wolves.objective = "kill";
        wolves.target = "wolf";
        wolves.count = 2;

        QuestDefinition raiders;
        raiders.id = "raiders";
        raiders.title = "Raiders";
        raiders.giver = "Hunter";
        raiders.after = "wolves";
        raiders.objective = "kill";
        raiders.target = "Raider";

        QuestDefinition shrine;
        shrine.id = "shrine";
        shrine.title = "Shrine";
        shrine.giver = "Elder";
        shrine.after = "herbs";
        shrine.objective = "reach";
        shrine.target = "Old Shrine";

        game.quests = {report, herbs, wolves, raiders, shrine};

        CharacterDefinition trader;
        trader.name = "Trader";
        trader.lines = {"Fine wares.", "Come again."};
        trader.sells = {{"potion", 12, 1}, {"arrow", 5, 10}};
        trader.buys = true;
        game.characters = {trader, CharacterDefinition{"Elder", {"Hello."}}, CharacterDefinition{"Guard", {}},
                           CharacterDefinition{"Hunter", {}}};
        game.world.startGold = 20;
        game.world.startItems = {{"arrow", 1}, {"potion", 1}};
        return game;
    }

    void testProgress() {
        GameProgress progress;
        progress.begin(sampleGame());
        CHECK(progress.gold == 20);
        CHECK(progress.count("arrow") == 1);

        // A quest chain: the second is only offered once the first is done.
        progress.openDialogue("Elder");
        CHECK(progress.getDialogue().text == "Hello.");
        progress.choose(0);
        CHECK(!progress.getDialogue().open);

        progress.openDialogue("Guard");
        CHECK(progress.getDialogue().text == "See the Elder.");
        progress.choose(0);
        CHECK(progress.getQuest("report").stage == QuestStage::Active);

        // Talking to the Elder is the objective and the hand-in; the next quest follows at once.
        progress.openDialogue("Elder");
        CHECK(progress.getDialogue().text == "Welcome.");
        progress.choose(0);
        CHECK(progress.getQuest("report").stage == QuestStage::Done);
        CHECK(progress.xp == 20);
        CHECK(progress.getDialogue().open);
        CHECK(progress.getDialogue().text == "Bring two moonleaf.");
        progress.choose(0);

        // A collect quest waits for the items and takes them.
        progress.give("herb", 1);
        progress.openDialogue("Elder");
        CHECK(progress.getDialogue().text == "Still waiting.");
        progress.closeDialogue();
        CHECK(progress.describe(progress.getQuests()[1]) == "Moonleaf: 1 / 2");

        progress.give("herb", 2);
        progress.openDialogue("Elder");
        CHECK(progress.getDialogue().text == "Thank you.");
        progress.choose(0);
        CHECK(progress.count("herb") == 1);
        CHECK(progress.gold == 50);
        CHECK(progress.level == 2);
        CHECK(progress.xp == 10);
        CHECK(progress.maxHealth() == 115.0f);
        CHECK(progress.bestWeaponDamage(40.0f) == 70.0f);

        // Kill quests count roles and name prefixes, and only while active.
        progress.onKill("wolf", "Grey Wolf");
        progress.openDialogue("Hunter");
        progress.choose(0);
        progress.onKill("wolf", "Grey Wolf");
        progress.onKill("bandit", "Raider 1");
        CHECK(progress.getQuest("wolves").count == 1);
        progress.onKill("wolf", "Grey Wolf");
        progress.onKill("wolf", "Grey Wolf");
        CHECK(progress.getQuest("wolves").count == 2);
        progress.openDialogue("Hunter");
        progress.choose(0);
        CHECK(progress.getQuest("wolves").stage == QuestStage::Done);
        CHECK(progress.getDialogue().open);   // the raiders, next
        progress.choose(0);
        progress.onKill("bandit", "Raider of the Hills");
        CHECK(progress.getQuest("raiders").count == 1);

        // Reach quests are completed by the session reporting the place.
        progress.openDialogue("Elder");
        progress.choose(0);
        CHECK(progress.reachTargets().size() == 1);
        progress.onReach("Somewhere Else");
        CHECK(progress.getQuest("shrine").count == 0);
        progress.onReach("Old Shrine");
        CHECK(progress.reachTargets().empty());

        // Trading.
        progress.give("wolf_pelt", 3);
        progress.openDialogue("Trader");
        const auto& options = progress.getDialogue().options;
        CHECK(options.size() == 4);
        CHECK(options[2] == "Sell pelts and goods for 18 gold");
        const int before = progress.gold;
        progress.choose(2);
        CHECK(progress.gold == before + 18);
        CHECK(progress.count("wolf_pelt") == 0);
        CHECK(progress.getDialogue().open);
        progress.choose(1);
        CHECK(progress.count("arrow") == 11);
        progress.gold = 3;
        progress.choose(0);
        CHECK(progress.count("potion") == 1);
        CHECK(progress.gold == 3);
        // With nothing left to sell, the offer to buy goes and Goodbye moves up.
        CHECK(progress.getDialogue().options.size() == 3);
        progress.choose(2);
        CHECK(!progress.getDialogue().open);

        // Flavor lines come in turn.
        progress.openDialogue("Trader");
        const std::string first = progress.getDialogue().text;
        progress.openDialogue("Trader");
        CHECK(progress.getDialogue().text != first);

        // Notices fade out.
        progress.notify("Hello", 1.0f);
        const size_t notices = progress.getNotices().size();
        progress.update(1.5f);
        CHECK(progress.getNotices().size() < notices);
    }

    void testInteractions(AssetManager& assets) {
        World world(assets);
        world.scene.game = sampleGame();
        world.scene.game.world.dayMinutes = 10.0f;
        world.scene.game.world.startHour = 21.0f;

        auto light = ObjectFactory::create(ObjectType::Light);
        light->setPosition(glm::vec3(0.0f, 20.0f, 0.0f));
        world.scene.add(light);

        Shape* herb = world.role(GameRole::Item, glm::vec3(0.0f, 0.5f, -1.5f), "Moonleaf patch", "herb:2");
        world.role(GameRole::Chest, glm::vec3(4.0f, 0.5f, -1.5f), "Old Chest", "potion:2, gold:25");
        world.role(GameRole::Sign, glm::vec3(-4.0f, 0.5f, -1.5f), "Milestone", "Ashford, 2 miles");
        world.role(GameRole::Bed, glm::vec3(8.0f, 0.5f, -1.5f), "Bed");
        Shape* window = world.role(GameRole::NightGlow, glm::vec3(0.0f, 5.0f, 9.0f), "Window");
        Shape* trader = world.role(GameRole::Villager, glm::vec3(-8.0f, 0.0f, -1.5f), "Trader");
        world.game.begin(world.scene);

        const GameProgress& progress = world.game.getProgress();
        const GameStatus& status = world.game.getStatus();
        CHECK(status.hasWorld);
        CHECK(status.night);
        CHECK(window->emissive);

        // The night sky is dark, the sun has become the moon, and light comes from a direction.
        glm::vec3 direction;
        CHECK(world.scene.getLightDirection(direction));
        CHECK(world.scene.clearColor.r + world.scene.clearColor.g + world.scene.clearColor.b < 0.6f);

        GameInput use;
        use.interact = true;

        world.run(GameInput(), 0.3f);
        CHECK(status.prompt == "Take Moonleaf");
        world.press(use);
        CHECK(progress.count("herb") == 2);
        CHECK(!herb->visible);

        auto walkTo = [&](float x) {
            world.player->setPosition(glm::vec3(x, 1.0f, 0.0f));
            world.player->velocity = glm::vec3(0.0f);
            world.run(GameInput(), 0.2f);
        };

        walkTo(4.0f);
        CHECK(status.prompt == "Open Old Chest");
        world.press(use);
        CHECK(progress.count("potion") == 3);
        CHECK(progress.gold == 45);
        world.run(GameInput(), 0.1f);
        CHECK(status.prompt.empty());   // opened once

        walkTo(-4.0f);
        world.press(use);
        CHECK(progress.getDialogue().open);
        CHECK(progress.getDialogue().text == "Ashford, 2 miles");
        GameInput reply;
        reply.number = 1;
        world.press(reply);
        CHECK(!progress.getDialogue().open);

        // Talking holds the player still, whatever the keys say.
        walkTo(-8.0f);
        CHECK(status.prompt == "Talk to Trader");
        world.press(use);
        CHECK(progress.getDialogue().open);
        const glm::vec3 standing = world.player->position;
        world.run(moving(1.0f, 0.0f), 0.5f);
        CHECK(glm::length(world.player->position - standing) < 0.05f);
        reply.number = (int)progress.getDialogue().options.size();
        world.press(reply);
        CHECK(!progress.getDialogue().open);
        (void)trader;

        // A potion heals, and only what is missing.
        GameInput drink;
        drink.usePotion = true;
        world.game.update(world.scene, GameInput(), 0.0f);
        world.press(drink);
        CHECK(progress.count("potion") == 3);  // full health: nothing drunk

        // Arrows are used up.
        GameInput shoot;
        shoot.number = 2;
        shoot.attack = true;
        world.run(shoot, 2.0f);
        CHECK(progress.count("arrow") == 0);

        // Sleeping passes the night.
        walkTo(8.0f);
        CHECK(status.prompt == "Sleep");
        world.press(use);
        CHECK(std::abs(status.hour - 7.0f) < 0.1f);
        CHECK(status.day == 2);
        CHECK(!status.night);
        CHECK(!window->emissive);
        CHECK(world.scene.clearColor.r + world.scene.clearColor.g + world.scene.clearColor.b > 1.0f);

        // Defeat costs some gold and returns the player to the start.
        Shape* wolf = world.role(GameRole::Wolf, glm::vec3(0.0f, 0.0f, 0.0f), "Wolf");
        (void)wolf;
    }

    void testWildlife(AssetManager& assets) {
        // A wolf pack hunts together and leaves pelts; a deer runs.
        World world(assets);
        world.scene.game = sampleGame();
        Shape* first = world.role(GameRole::Wolf, glm::vec3(0.0f, 0.0f, -10.0f), "Grey Wolf");
        Shape* second = world.role(GameRole::Wolf, glm::vec3(10.0f, 0.0f, -18.0f), "Grey Wolf");
        Shape* deer = world.role(GameRole::Deer, glm::vec3(-8.0f, 0.0f, 0.0f), "Deer");
        world.game.begin(world.scene);

        const glm::vec3 secondStart = second->position;
        world.run(GameInput(), 2.0f);
        // The deer runs until it is out of reach, then settles there.
        CHECK(glm::length(glm::vec2(deer->position.x - world.player->position.x, deer->position.z - world.player->position.z)) > 10.5f);
        CHECK(glm::length(second->position - secondStart) > 4.0f);   // came with the first
        CHECK(world.game.getStatus().health < 100.0f);

        GameInput attack;
        attack.attack = true;
        for (int i = 0; i < 600 && world.game.getProgress().count("wolf_pelt") < 2; i++) {
            // Keep facing whichever wolf is nearer.
            Shape* target = glm::length(first->position - world.player->position) <
                            glm::length(second->position - world.player->position) ? first : second;
            if (target->rotation.x > 80.0f) target = target == first ? second : first;
            const glm::vec3 to = target->position - world.player->position;
            (void)to;
            world.press(attack);
        }
        CHECK(world.game.getProgress().count("wolf_pelt") >= 1);
    }

    void testLimbs(AssetManager& assets) {
        World world(assets);
        Shape* villager = world.role(GameRole::Villager, glm::vec3(10.0f, 0.0f, 0.0f), "Walker");
        auto leg = ObjectFactory::create(ObjectType::Cube);
        Shape* limb = static_cast<Shape*>(world.scene.add(leg, villager));
        limb->gameRole = GameRole::Limb;
        limb->limbSwing = 30.0f;
        world.game.begin(world.scene);

        float most = 0.0f;
        for (int i = 0; i < 600; i++) {
            world.run(GameInput(), 1.0f / 60.0f);
            most = std::max(most, std::abs(limb->rotation.x));
        }
        CHECK(most > 10.0f);
        CHECK(most <= 30.0f);
    }

    void testWorldRendering(AssetManager& assets, const fs::path& root) {
        writeCutoutTexture(root / "Assets/Textures/cutout.tga");

        SceneGraph scene;
        scene.clearColor = glm::vec3(0.0f, 0.0f, 1.0f);
        auto card = ObjectFactory::create(ObjectType::Cube);
        card->setScale(glm::vec3(4.0f, 4.0f, 0.1f));
        card->setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        Shape* cardShape = static_cast<Shape*>(scene.add(card));
        cardShape->setTexture("texture_albedo", assets.getTexture("Assets/Textures/cutout.tga"));
        cardShape->setColor(glm::vec3(1.0f));

        Renderer renderer(1024);
        RenderTarget target;
        target.resize(WIDTH, HEIGHT);
        CameraView camera;
        camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
        camera.view = glm::lookAt(camera.position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        camera.projection = glm::perspective(glm::radians(60.0f), (float)WIDTH / HEIGHT, 0.1f, 100.0f);

        RenderSettings unlit;
        unlit.lighting = false;
        renderer.render(scene, camera, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, unlit);
        Image image = readImage(target.framebuffer());

        // Transparent texels show what is behind them; opaque ones do not.
        auto pixel = [&](const Image& img, int x, int y) { return img.data() + ((size_t)y * WIDTH + x) * 4; };
        const unsigned char* left = pixel(image, WIDTH / 2 - 30, HEIGHT / 2);
        const unsigned char* right = pixel(image, WIDTH / 2 + 30, HEIGHT / 2);
        CHECK(left[1] > 150 && left[2] < 100);
        CHECK(right[2] > 200 && right[1] < 50);

        // Emissive surfaces ignore the light entirely.
        cardShape->setTexture("texture_albedo", nullptr);
        cardShape->setColor(glm::vec3(0.8f, 0.4f, 0.2f));
        cardShape->emissive = true;
        RenderSettings lit;
        renderer.render(scene, camera, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, lit);
        const Image glowing = readImage(target.framebuffer());
        renderer.render(scene, camera, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, unlit);
        CHECK(differingPixels(glowing, readImage(target.framebuffer())) == 0);

        // Water swells over time.
        {
            std::string grid;
            for (int z = 0; z <= 10; z++) for (int x = 0; x <= 10; x++) grid += "v " + std::to_string(x - 5) + " 0 " + std::to_string(z - 5) + "\n";
            for (int z = 0; z < 10; z++) for (int x = 0; x < 10; x++) {
                const int a = z * 11 + x + 1;
                grid += "f " + std::to_string(a) + " " + std::to_string(a + 11) + " " + std::to_string(a + 12) + " " + std::to_string(a + 1) + "\n";
            }
            writeObj(root / "Assets/Models/water.obj", grid);

            SceneGraph lake;
            auto surface = std::make_shared<MeshShape>();
            surface->setMesh(assets.getMesh("Assets/Models/water.obj"), "Assets/Models/water.obj");
            surface->waveHeight = 0.5f;
            surface->setColor(glm::vec3(0.2f, 0.4f, 0.8f));
            lake.add(surface);
            CameraView above;
            above.position = glm::vec3(0.0f, 3.0f, 6.0f);
            above.view = glm::lookAt(above.position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            above.projection = camera.projection;
            renderer.render(lake, above, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, lit);
            const Image calm = readImage(target.framebuffer());
            lake.update(1.3f);
            renderer.render(lake, above, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, lit);
            CHECK(differingPixels(calm, readImage(target.framebuffer())) > 500);
        }

        // Sunlight casts shadows far from the origin, where a fixed shadow box would not reach.
        {
            SceneGraph far;
            const glm::vec3 place(300.0f, 0.0f, -260.0f);
            Shape* floor = addShape(far, ObjectType::Cube, place + glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(30.0f, 1.0f, 30.0f), glm::vec3(0.8f));
            Shape* tower = addShape(far, ObjectType::Cube, place + glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f, 6.0f, 1.0f), glm::vec3(0.8f));
            (void)floor;
            (void)tower;
            auto sun = ObjectFactory::create(ObjectType::Light);
            auto* sunLight = static_cast<LightObject*>(far.add(sun));
            sunLight->type = LightType::Directional;
            sunLight->setWorldMatrix(glm::inverse(glm::lookAt(glm::vec3(0.0f), glm::normalize(glm::vec3(1.0f, -1.0f, 0.4f)), glm::vec3(0, 1, 0))));

            CameraView view;
            view.position = place + glm::vec3(0.0f, 9.0f, 14.0f);
            view.view = glm::lookAt(view.position, place, glm::vec3(0.0f, 1.0f, 0.0f));
            view.projection = camera.projection;

            RenderSettings shadows;
            renderer.render(far, view, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, shadows);
            const Image withShadows = readImage(target.framebuffer());
            RenderSettings none;
            none.shadows = false;
            renderer.render(far, view, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, none);
            CHECK(differingPixels(withShadows, readImage(target.framebuffer())) > 300);
        }

        // Culling leaves out what is behind the camera, and only that.
        {
            SceneGraph scene2;
            Shape* front = addShape(scene2, ObjectType::Cube, glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            (void)front;
            renderer.render(scene2, camera, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, unlit);
            const Image one = readImage(target.framebuffer());
            addShape(scene2, ObjectType::Cube, glm::vec3(0.0f, 0.0f, 20.0f), glm::vec3(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            renderer.render(scene2, camera, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, unlit);
            CHECK(differingPixels(one, readImage(target.framebuffer())) == 0);
            // At the very edge of the view, still drawn.
            addShape(scene2, ObjectType::Cube, glm::vec3(3.9f, 0.0f, 0.0f), glm::vec3(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            renderer.render(scene2, camera, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, unlit);
            CHECK(differingPixels(one, readImage(target.framebuffer())) > 50);
        }

        // A fire lights the ground near it, per pixel and per vertex, and only while it burns.
        {
            SceneGraph night;
            night.ambientColor = glm::vec3(0.05f);
            auto ground = std::make_shared<MeshShape>();
            ground->setMesh(assets.getMesh("Assets/Models/water.obj"), "Assets/Models/water.obj");
            ground->setColor(glm::vec3(0.8f));
            night.add(ground);
            auto moon = ObjectFactory::create(ObjectType::Light);
            auto* moonLight = static_cast<LightObject*>(night.add(moon));
            moonLight->type = LightType::Directional;
            moonLight->color = glm::vec3(0.05f);
            moonLight->setWorldMatrix(glm::inverse(glm::lookAt(glm::vec3(0.0f), glm::vec3(0.2f, -1.0f, 0.1f), glm::vec3(0, 0, 1))));
            Shape* fire = addShape(night, ObjectType::Cube, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.2f), glm::vec3(1.0f, 0.6f, 0.3f));
            fire->emissive = true;

            CameraView above;
            above.position = glm::vec3(0.0f, 6.0f, 6.0f);
            above.view = glm::lookAt(above.position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            above.projection = camera.projection;
            auto brightness = [&](const Image& img) {
                double total = 0.0;
                for (size_t i = 0; i < img.size(); i += 4) total += img[i] + img[i + 1];
                return total;
            };
            for (RenderMode mode : {RenderMode::Modern, RenderMode::PS2}) {
                RenderSettings settings = RenderSettings::preset(mode);
                fire->lightRange = 0.0f;
                renderer.render(night, above, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, settings);
                const double dark = brightness(readImage(target.framebuffer()));
                fire->lightRange = 5.0f;
                renderer.render(night, above, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, settings);
                const double lit = brightness(readImage(target.framebuffer()));
                fire->emissive = false;
                renderer.render(night, above, RenderOutput{target.framebuffer(), WIDTH, HEIGHT}, settings);
                const double out = brightness(readImage(target.framebuffer()));
                fire->emissive = true;
                CHECK(lit > dark * 1.5);
                CHECK(out < lit * 0.8);
            }
        }
    }

    void testOpenWorldPhysics(AssetManager& assets, const fs::path& root) {
        // A wall set at an angle: the player slides along it and never gets inside.
        {
            World world(assets);
            Shape* wall = world.box(glm::vec3(0.0f, 1.0f, -4.0f), glm::vec3(12.0f, 2.0f, 1.0f));
            wall->setRotation(glm::vec3(0.0f, 30.0f, 0.0f));
            world.game.begin(world.scene);
            float deepest = 0.0f;
            for (int i = 0; i < 120; i++) {
                world.press(moving(1.0f, 0.0f));
                const SolidBox box = SolidBox::of(*wall);
                const glm::vec2 offset = glm::vec2(world.player->position.x, world.player->position.z) - glm::vec2(box.center.x, box.center.z);
                deepest = std::max(deepest, 0.5f + 0.4f - std::abs(glm::dot(offset, box.axisZ)));
            }
            CHECK(deepest < 0.02f);
            CHECK(std::abs(world.player->position.x) > 1.5f);   // it slid, rather than stopping
        }

        // Running diagonally at the corner of a box carries the player round it.
        {
            World world(assets);
            world.box(glm::vec3(0.6f, 1.0f, -3.0f), glm::vec3(1.0f, 2.0f, 1.0f));
            world.game.begin(world.scene);
            world.run(moving(1.0f, 0.0f), 2.0f);
            CHECK(world.player->position.z < -5.0f);
        }

        // Ground in pieces is one ground; a thin wall is not run through at speed.
        {
            writeObj(root / "Assets/Models/left.obj", "v -40 0 -40\nv 0 0 -40\nv 0 0 40\nv -40 0 40\nf 1 4 3 2\n");
            writeObj(root / "Assets/Models/right.obj", "v 0 2 -40\nv 40 2 -40\nv 40 2 40\nv 0 2 40\nf 1 4 3 2\n");
            World world(assets, "Assets/Models/left.obj");
            auto right = std::make_shared<MeshShape>();
            right->setMesh(assets.getMesh("Assets/Models/right.obj"), "Assets/Models/right.obj");
            right->gameRole = GameRole::Ground;
            world.scene.add(right);
            world.player->setPosition(glm::vec3(10.0f, 4.0f, 0.0f));
            world.game.begin(world.scene);
            world.run(GameInput(), 1.0f);
            CHECK(std::abs(world.player->position.y - world.player->scale.y * 0.5f - 2.0f) < 0.05f);
            world.player->setPosition(glm::vec3(-10.0f, 1.5f, 0.0f));
            world.run(GameInput(), 1.0f);
            CHECK(std::abs(world.player->position.y - world.player->scale.y * 0.5f) < 0.05f);
        }

        // Where two pieces of river overlap, the water is the piece whose middle is nearer.
        {
            CharacterSurroundings water;
            water.waters.push_back({1.0f, glm::vec4(-10.0f, -10.0f, 10.0f, 10.0f)});
            water.waters.push_back({3.0f, glm::vec4(8.0f, -10.0f, 28.0f, 10.0f)});
            CHECK(water.waterAt(glm::vec2(0.0f, 0.0f))->level == 1.0f);
            CHECK(water.waterAt(glm::vec2(8.5f, 0.0f))->level == 1.0f);
            CHECK(water.waterAt(glm::vec2(20.0f, 0.0f))->level == 3.0f);
            CHECK(water.waterAt(glm::vec2(40.0f, 0.0f)) == nullptr);
        }

        // A bandit behind a wall finds its way round, and keeps out of the player.
        {
            World world(assets);
            world.box(glm::vec3(0.0f, 1.0f, -4.0f), glm::vec3(8.0f, 2.0f, 0.6f));
            Shape* bandit = world.role(GameRole::Bandit, glm::vec3(0.0f, 0.0f, -8.0f), "Raider");
            world.game.begin(world.scene);
            world.run(GameInput(), 8.0f);
            CHECK(world.game.getStatus().health < 100.0f);
            const float apart = glm::length(glm::vec2(bandit->position.x - world.player->position.x, bandit->position.z - world.player->position.z));
            CHECK(apart > 0.75f);
        }

        // A pack keeps apart, and its dead fall over on their sides.
        {
            World world(assets);
            Shape* a = world.role(GameRole::Wolf, glm::vec3(1.0f, 0.0f, -6.0f), "Grey Wolf");
            Shape* b = world.role(GameRole::Wolf, glm::vec3(1.2f, 0.0f, -6.2f), "Grey Wolf");
            Shape* c = world.role(GameRole::Wolf, glm::vec3(0.8f, 0.0f, -6.1f), "Grey Wolf");
            world.game.begin(world.scene);
            world.run(GameInput(), 3.0f);
            auto gap = [](Shape* x, Shape* y) { return glm::length(glm::vec2(x->position.x - y->position.x, x->position.z - y->position.z)); };
            CHECK(gap(a, b) > 0.7f && gap(b, c) > 0.7f && gap(a, c) > 0.7f);
            GameInput attack;
            attack.attack = true;
            for (int i = 0; i < 900 && world.game.isAlive(a->id) + world.game.isAlive(b->id) + world.game.isAlive(c->id) == 3; i++) world.press(attack);
            world.run(GameInput(), 1.0f);
            Shape* dead = !world.game.isAlive(a->id) ? a : (!world.game.isAlive(b->id) ? b : c);
            CHECK(!world.game.isAlive(dead->id));
            CHECK(std::abs(dead->rotation.z) > 80.0f);
        }
    }

    void testCreatures(AssetManager& assets) {
        // A dragon wakes, takes to the air, and breathes fire at the player.
        {
            World world(assets);
            Shape* dragon = world.role(GameRole::Dragon, glm::vec3(0.0f, 0.0f, -30.0f), "Cinderwing");
            std::vector<Shape*> fires;
            for (int i = 0; i < 3; i++) fires.push_back(world.role(GameRole::Fireball, glm::vec3(0.0f, -5.0f, 0.0f), "Fire"));
            world.game.begin(world.scene);
            CHECK(!fires[0]->visible);
            float highest = 0.0f;
            bool breathed = false;
            for (int i = 0; i < 60 * 12; i++) {
                world.press(GameInput());
                highest = std::max(highest, dragon->position.y);
                for (Shape* fire : fires) breathed = breathed || fire->visible;
            }
            CHECK(highest > 8.0f);
            CHECK(breathed);
            CHECK(world.game.getStatus().health < 100.0f);
            CHECK(world.game.isAlive(dragon->id));
        }
        {
            writeObj(fs::path(assets.resolve("Assets/Models/flat.obj")).parent_path() / "wide.obj",
                     "v -200 0 -200\nv 200 0 -200\nv 200 0 200\nv -200 0 200\nf 1 4 3 2\n");
            // Once the player is far out of its sight, it flies home and settles there.
            World world(assets, "Assets/Models/wide.obj");
            Shape* dragon = world.role(GameRole::Dragon, glm::vec3(0.0f, 0.0f, -30.0f), "Cinderwing");
            world.game.begin(world.scene);
            world.run(GameInput(), 3.0f);
            CHECK(dragon->position.y > 2.0f);
            world.player->setPosition(glm::vec3(0.0f, 1.0f, 150.0f));
            world.player->velocity = glm::vec3(0.0f);
            world.run(GameInput(), 25.0f);
            CHECK(glm::length(dragon->position - glm::vec3(0.0f, 0.0f, -30.0f)) < 1.0f);
        }

        // Birds take off when someone comes near, and settle again once they are gone.
        {
            World world(assets);
            Shape* crow = world.role(GameRole::Bird, glm::vec3(0.0f, 0.0f, -3.0f), "Crow");
            world.player->setPosition(glm::vec3(0.0f, 1.0f, 20.0f));
            world.game.begin(world.scene);
            world.run(GameInput(), 1.0f);
            CHECK(crow->position.y < 0.1f);
            world.player->setPosition(glm::vec3(0.0f, 1.0f, 0.0f));
            world.player->velocity = glm::vec3(0.0f);
            float highest = 0.0f;
            for (int i = 0; i < 180; i++) {
                world.press(GameInput());
                highest = std::max(highest, crow->position.y);
            }
            CHECK(highest > 3.0f);
            world.player->setPosition(glm::vec3(0.0f, 1.0f, 35.0f));
            world.player->velocity = glm::vec3(0.0f);
            world.run(GameInput(), 25.0f);
            CHECK(crow->position.y < 0.1f);
        }

        // Rabbits bolt; boars and bears charge, and a bear hits much harder than a wolf.
        {
            World world(assets);
            Shape* rabbit = world.role(GameRole::Rabbit, glm::vec3(-4.0f, 0.0f, 0.0f), "Rabbit");
            world.game.begin(world.scene);
            world.run(GameInput(), 2.0f);
            CHECK(glm::length(glm::vec2(rabbit->position.x, rabbit->position.z)) > 6.5f);
        }
        auto hurtBy = [&](GameRole role) {
            World world(assets);
            world.role(role, glm::vec3(0.0f, 0.0f, -3.0f), "Beast");
            world.game.begin(world.scene);
            world.run(GameInput(), 2.5f);
            return 100.0f - world.game.getStatus().health;
        };
        const float wolf = hurtBy(GameRole::Wolf);
        const float boar = hurtBy(GameRole::Boar);
        const float bear = hurtBy(GameRole::Bear);
        CHECK(wolf > 0.0f && boar > 0.0f);
        CHECK(bear > wolf);
    }

    void testOpenWorldSerialization(AssetManager& assets) {
        SceneGraph scene;
        scene.game = sampleGame();
        scene.game.world.dayMinutes = 12.0f;
        scene.ambientColor = glm::vec3(0.2f, 0.3f, 0.4f);
        Shape* chest = addShape(scene, ObjectType::Cube, glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f));
        chest->gameRole = GameRole::Chest;
        chest->gameParameter = "potion:2, gold:25";
        chest->emissive = true;
        chest->waveHeight = 0.3f;
        chest->limbSwing = 12.0f;
        chest->limbPhase = 180.0f;
        chest->limbAxis = 2;
        chest->sway = 0.4f;
        chest->lightRange = 7.5f;
        chest->lightColor = glm::vec3(0.9f, 0.5f, 0.2f);
        Shape* dragon = addShape(scene, ObjectType::Cube, glm::vec3(4.0f), glm::vec3(1.0f), glm::vec3(1.0f));
        dragon->gameRole = GameRole::Dragon;
        scene.gradientSky = true;
        auto sun = ObjectFactory::create(ObjectType::Light);
        static_cast<LightObject*>(scene.add(sun))->type = LightType::Directional;

        const std::string text = SceneSerializer::serialize(scene);
        SceneGraph loaded;
        std::string error;
        CHECK(SceneSerializer::deserialize(text, loaded, assets, &error));
        CHECK(SceneSerializer::serialize(loaded) == text);
        CHECK(loaded.game.quests.size() == 5);
        CHECK(loaded.game.quests[1].after == "report");
        CHECK(loaded.game.characters[0].sells.size() == 2);
        CHECK(loaded.game.world.dayMinutes == 12.0f);
        CHECK(loaded.getLight()->isDirectional());
        Shape* restored = loaded.findAs<Shape>(chest->id);
        CHECK(restored && restored->gameParameter == "potion:2, gold:25" && restored->emissive && restored->limbPhase == 180.0f);
        CHECK(restored && restored->limbAxis == 2 && restored->sway == 0.4f && restored->lightRange == 7.5f &&
              restored->lightColor == glm::vec3(0.9f, 0.5f, 0.2f));
        CHECK(loaded.findAs<Shape>(dragon->id)->gameRole == GameRole::Dragon);
        CHECK(loaded.gradientSky);

        // The new image settings keep their values, and out-of-range ones are brought back in.
        RenderSettings graded;
        graded.textureFilter = TextureFilter::NearestMipmapped;
        graded.vignette = 0.35f;
        graded.saturation = 1.2f;
        graded.scanlines = 0.3f;
        graded.crtCurvature = 0.2f;
        graded.chromaticAberration = 0.15f;
        graded.filmGrain = 0.1f;
        graded.bloom = 0.25f;
        RenderSettings back;
        CHECK(RenderSettings::fromJson(graded.toJson(), back));
        CHECK(back.textureFilter == TextureFilter::NearestMipmapped && back.vignette == 0.35f && back.saturation == 1.2f);
        CHECK(back.scanlines == 0.3f && back.crtCurvature == 0.2f && back.chromaticAberration == 0.15f);
        CHECK(back.filmGrain == 0.1f && back.bloom == 0.25f);
        CHECK(RenderSettings::fromJson(R"({"vignette": 3, "saturation": -1, "scanlines": 5, "filmGrain": -2})", back));
        CHECK(back.vignette == 1.0f && back.saturation == 0.0f && back.scanlines == 1.0f && back.filmGrain == 0.0f);

        // Scenes without a game keep their files as they were.
        SceneGraph plain;
        CHECK(SceneSerializer::serialize(plain).find("\"game\"") == std::string::npos);

        // A malformed game section is refused, not half-loaded.
        SceneGraph broken;
        CHECK(!SceneSerializer::deserialize(R"({"version":1,"objects":[],"game":{"quests":7}})", broken, assets, &error));
    }

    void testHeadTrackingCamera() {
        GameLevel level;
        GameSession game;
        game.begin(level.scene);
        game.setFirstPerson(level.scene, true);
        CameraObject* camera = level.scene.getMainCamera();
        CHECK(camera != nullptr);
        if (!camera) return;

        auto forwardOf = [&] { return -glm::normalize(glm::vec3(camera->getWorldMatrix()[2])); };

        level.run(game, GameInput(), 0.2f);
        const glm::vec3 straight = forwardOf();
        const glm::vec3 eye = glm::vec3(camera->getWorldMatrix()[3]);

        // The head turns the view in place, and what is heard follows it; the player does not turn.
        GameInput turned;
        turned.headRotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        game.update(level.scene, turned, 1.0f / 120.0f);
        const glm::vec3 looking = forwardOf();
        const float angle = glm::degrees(std::acos(glm::clamp(glm::dot(straight, looking), -1.0f, 1.0f)));
        CHECK(std::fabs(angle - 30.0f) < 0.5f);
        CHECK(glm::length(glm::vec3(camera->getWorldMatrix()[3]) - eye) < 0.01f);
        CHECK(glm::length(game.getStatus().listenerForward - looking) < 0.001f);

        // Without a tracked head the camera is exactly where the rules put it.
        game.update(level.scene, GameInput(), 1.0f / 120.0f);
        CHECK(glm::length(forwardOf() - straight) < 0.001f);
    }

    void testGameSerialization(AssetManager& assets) {
        GameLevel level;
        Shape* hazard = level.addRole(GameRole::Hazard, glm::vec3(1.0f));
        hazard->patrolOffset = glm::vec3(0.0f, 2.0f, 0.0f);
        hazard->patrolSeconds = 4.5f;

        SceneGraph loaded;
        std::string error;
        CHECK(SceneSerializer::deserialize(SceneSerializer::serialize(level.scene), loaded, assets, &error));
        CHECK(loaded.getPlayer() != nullptr);

        Shape* restored = loaded.findAs<Shape>(hazard->id);
        CHECK(restored && restored->gameRole == GameRole::Hazard);
        CHECK(restored && restored->patrolSeconds == 4.5f);
        CHECK(restored && restored->patrolOffset == glm::vec3(0.0f, 2.0f, 0.0f));
        CHECK(!loaded.getMainCamera()->firstPerson);
    }

    void testPlayRestart(Project& project, AssetManager& assets) {
        SceneDocument document(project, assets);
        document.createNew();

        SceneGraph& scene = document.getScene();
        auto playerObject = ObjectFactory::create(ObjectType::Player);
        static_cast<Player*>(playerObject.get())->gameControls = true;
        playerObject->setPosition(glm::vec3(0.0f, 10.0f, 0.0f));
        const ObjectId playerId = scene.add(playerObject)->id;

        auto pickupObject = ObjectFactory::create(ObjectType::Cube);
        pickupObject->setPosition(glm::vec3(0.0f, 9.0f, 0.0f));
        Shape* pickup = static_cast<Shape*>(scene.add(pickupObject));
        pickup->gameRole = GameRole::Pickup;
        pickup->isStatic = true;
        const ObjectId pickupId = pickup->id;

        PlayController play(document);
        play.play();
        for (int i = 0; i < 30; i++) {
            play.update(1.0f / 60.0f, GameInput());
        }
        CHECK(play.getGameStatus().collected == 1);

        // Restarting puts everything back and stays in Play mode.
        play.restart();
        CHECK(play.isPlaying());
        CHECK(play.getGameStatus().collected == 0);
        CHECK(play.getGameStatus().seconds == 0.0f);
        CHECK(document.getScene().find(playerId)->position.y == 10.0f);
        CHECK(document.getScene().find(pickupId)->visible);

        play.stop();
        CHECK(document.getScene().findAs<Shape>(pickupId)->hasCollision);
    }

    void testPlayMode(Project& project, AssetManager& assets) {
        SceneDocument document(project, assets);
        document.createNew();

        Shape* box = addShape(document.getScene(), ObjectType::Cube, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f), glm::vec3(1.0f));
        box->useGravity = true;
        const ObjectId id = box->id;

        PlayController play(document);
        play.play();
        CHECK(play.isPlaying());
        CHECK(!document.isDirty());

        // Paused means frozen.
        play.update(0.05f, GameInput());
        const float afterStep = document.getScene().find(id)->position.y;
        CHECK(afterStep < 5.0f);
        play.togglePause();
        CHECK(play.isPaused());
        play.update(0.05f, GameInput());
        CHECK(document.getScene().find(id)->position.y == afterStep);
        play.togglePause();

        // Nothing done during play counts as an edit, or survives it.
        document.getScene().find(id)->name = "Changed";
        document.markDirty();
        CHECK(!document.isDirty());

        play.stop();
        CHECK(play.isStopped());
        SceneObject* restored = document.getScene().find(id);
        CHECK(restored != nullptr);
        if (restored) {
            CHECK(restored->position.y == 5.0f);
            CHECK(restored->name == "Cube");
        }

        // Edits count again afterwards.
        document.markDirty();
        CHECK(document.isDirty());

        // A failed open keeps the scene that was open.
        std::string error;
        const size_t objects = document.getScene().getObjects().size();
        CHECK(!document.open("Assets/Scenes/DoesNotExist.scene", &error));
        CHECK(document.getScene().getObjects().size() == objects);

        CHECK(document.saveAs("Assets/Scenes/Saved Копія.scene", &error));
        CHECK(!document.isDirty());
        CHECK(project.startupScene == "Assets/Scenes/Saved Копія.scene");
        CHECK(fs::exists(project.getRoot() / fs::u8path("Assets/Scenes/Saved Копія.scene")));
    }
}

int main() {
    GlWindow::Config config;
    config.width = 64;
    config.height = 64;
    config.title = "render tests";
    config.visible = false;
    GlWindow window(config);

    // A machine without a display cannot run these; 77 tells CTest it is a skip, not a failure.
    if (!window.isValid()) {
        std::printf("render_tests: no OpenGL context available, skipping\n");
        return 77;
    }

    TempDir temp;
    ProjectCreateOptions options;
    options.name = "Render Tests";
    options.location = temp.path;
    auto project = Project::create(options);
    if (!project) return 1;

    writeCheckerTexture(project->assetsDir() / "Textures" / "checker.tga");

    {
        AssetManager assets;
        assets.setProject(project->getRoot(), project->libraryDir());

        testRenderSettings(assets);
        testPostProcessing(assets);
        testViewportRenderer(assets);
        testHierarchy();
        testTransformDecomposition();
        testSerialization(assets);
        testDuplicate(assets);
        testMeshAssets(assets, project->getRoot());
        testPhysics();
        testGameRules();
        testHeadTrackingCamera();
        testGameSerialization(assets);
        testWorldGame(assets, project->getRoot());
        testCharacterController(assets, project->getRoot());
        testEffects(assets);
        testDiving(assets);
        testTerrain(assets);
        testRayTracedShadows();
        testFireDamage(assets);
        testProps(assets);
        testProgress();
        testInteractions(assets);
        testWildlife(assets);
        testLimbs(assets);
        testWorldRendering(assets, project->getRoot());
        testOpenWorldSerialization(assets);
        testOpenWorldPhysics(assets, project->getRoot());
        testCreatures(assets);
        testPlayRestart(*project, assets);
        testPlayMode(*project, assets);
    }

    return testSummary("render_tests");
}
