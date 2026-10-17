#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <atomic>
#include <memory>

// In-app updates for the macOS standalone app.
// Checks the latest GitHub release of the (private) repo with a read-only token injected at build time
// (BRUME_UPDATE_TOKEN), downloads Brume-macOS.zip, replaces the app and the user plugins, and relaunches.
class Updater : private juce::Thread
{
public:
    struct Status
    {
        enum class State { idle, available, downloading, installing, failed } state = State::idle;
        juce::String latestVersion, message;
        float progress = 0.0f;
    };

    Updater();
    ~Updater() override;

    static bool isSupported();            // macOS standalone with a token compiled in
    static juce::String currentVersion();

    void checkInBackground();
    void installInBackground();

    std::function<void (const Status&)> onStatus;   // called on the message thread

private:
    void run() override;
    void post (Status s);
    bool fetchLatest();
    bool downloadAndInstall();

    enum class Job { none, check, install } job = Job::none;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    juce::String latestTag, assetApiUrl;
};

class UpdateBanner : public juce::Component
{
public:
    UpdateBanner();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Updater updater;
    Updater::Status status;
    juce::TextButton button { "Mettre a jour" };
};
