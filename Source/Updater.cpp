#include "Updater.h"
#if ! JUCE_WINDOWS
 #include <unistd.h>
#endif

#ifndef BRUME_UPDATE_TOKEN
 #define BRUME_UPDATE_TOKEN ""
#endif
#ifndef BRUME_UPDATE_REPO
 #define BRUME_UPDATE_REPO "contactdefarge-pixel/brume"
#endif

namespace
{
    const char* kAssetName = "Brume-macOS.zip";

    juce::String fr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    // "v1.0.12" -> comparable number
    juce::int64 versionNumber (juce::String v)
    {
        v = v.trimCharactersAtStart ("vV");
        auto parts = juce::StringArray::fromTokens (v, ".", "");
        juce::int64 n = 0;
        for (int i = 0; i < 3; ++i)
            n = n * 100000 + (i < parts.size() ? parts[i].getLargeIntValue() : 0);
        return n;
    }

    juce::String authHeaders (const juce::String& accept)
    {
        return "Authorization: Bearer " + juce::String (BRUME_UPDATE_TOKEN) + "\r\n"
               "Accept: " + accept + "\r\n"
               "X-GitHub-Api-Version: 2022-11-28\r\n"
               "User-Agent: Brume-Updater";
    }
}

Updater::Updater() : juce::Thread ("Brume updater") {}
Updater::~Updater() { alive->store (false); stopThread (4000); }

bool Updater::isSupported()
{
   #if JUCE_MAC
    return juce::JUCEApplicationBase::isStandaloneApp() && juce::String (BRUME_UPDATE_TOKEN).isNotEmpty();
   #else
    return false;
   #endif
}

juce::String Updater::currentVersion()
{
   #ifdef JucePlugin_VersionString
    return JucePlugin_VersionString;
   #else
    return "0.0.0";
   #endif
}

void Updater::checkInBackground()
{
    if (! isSupported() || isThreadRunning()) return;

    auto log = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/Brume-update.log");
    if (log.existsAsFile())
    {
        const auto text = log.loadFileAsString();
        log.moveFileTo (log.withFileExtension ("previous.log"));
        if (text.contains ("UPDATE_FAILED"))
        {
            Status st;
            st.state = Status::State::failed;
            st.message = fr ("macOS a bloqué le remplacement : glisse la nouvelle Brume.app (ouverte dans le Finder) dans Applications.");
            post (st);
            return;
        }
    }
    job = Job::check;
    startThread();
}

void Updater::installInBackground()
{
    if (! isSupported() || isThreadRunning() || assetApiUrl.isEmpty()) return;
    job = Job::install;
    startThread();
}

void Updater::post (Status s)
{
    juce::MessageManager::callAsync ([this, s, flag = alive]
    {
        if (flag->load() && onStatus) onStatus (s);
    });
}

void Updater::run()
{
    if (job == Job::check)
    {
        if (fetchLatest() && versionNumber (latestTag) > versionNumber (currentVersion()))
        {
            Status s;
            s.state = Status::State::available;
            s.latestVersion = latestTag;
            post (s);
        }
    }
    else if (job == Job::install)
    {
        if (! downloadAndInstall())
        {
            Status s;
            s.state = Status::State::failed;
            s.message = fr ("La mise à jour a échoué. Réessaie plus tard ou télécharge-la sur GitHub.");
            s.latestVersion = latestTag;
            post (s);
        }
    }
    job = Job::none;
}

bool Updater::fetchLatest()
{
    int status = 0;
    juce::URL url ("https://api.github.com/repos/" BRUME_UPDATE_REPO "/releases/latest");
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                             .withExtraHeaders (authHeaders ("application/vnd.github+json"))
                                             .withConnectionTimeoutMs (10000)
                                             .withStatusCode (&status));
    if (stream == nullptr || status != 200) return false;

    const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
    latestTag = json.getProperty ("tag_name", {}).toString();
    assetApiUrl.clear();
    if (auto* assets = json.getProperty ("assets", {}).getArray())
        for (auto& a : *assets)
            if (a.getProperty ("name", {}).toString() == kAssetName)
                assetApiUrl = a.getProperty ("url", {}).toString();
    return latestTag.isNotEmpty() && assetApiUrl.isNotEmpty();
}

bool Updater::downloadAndInstall()
{
   #if JUCE_WINDOWS
    return false;
   #else
    Status s;
    s.state = Status::State::downloading;
    s.latestVersion = latestTag;
    post (s);

    // 1) ask GitHub for the asset; it answers with a redirect to a signed download link (no token needed there)
    int status = 0;
    juce::StringPairArray headers;
    juce::URL apiUrl (assetApiUrl);
    auto first = apiUrl.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                               .withExtraHeaders (authHeaders ("application/octet-stream"))
                                               .withConnectionTimeoutMs (15000)
                                               .withNumRedirectsToFollow (0)
                                               .withResponseHeaders (&headers)
                                               .withStatusCode (&status));
    std::unique_ptr<juce::InputStream> body;
    if (status >= 300 && status < 400)
    {
        juce::String location;
        for (auto& k : headers.getAllKeys())
            if (k.equalsIgnoreCase ("location")) location = headers[k].trim();
        if (location.isEmpty()) return false;
        first.reset();
        int status2 = 0;
        body = juce::URL (location).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                            .withConnectionTimeoutMs (15000)
                                                            .withStatusCode (&status2));
        if (status2 != 200) return false;
    }
    else if (status == 200)
    {
        body = std::move (first);
    }
    if (body == nullptr) return false;

    // 2) download to a temp folder
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BrumeUpdate");
    tmp.deleteRecursively();
    tmp.createDirectory();
    auto zip = tmp.getChildFile (kAssetName);
    {
        juce::FileOutputStream out (zip);
        if (! out.openedOk()) return false;
        const auto total = body->getTotalLength();
        juce::HeapBlock<char> buf (65536);
        juce::int64 done = 0;
        for (;;)
        {
            if (threadShouldExit()) return false;
            const int n = body->read (buf, 65536);
            if (n <= 0) break;
            out.write (buf, (size_t) n);
            done += n;
            if (total > 0 && (done % (1 << 20)) < 65536)
            {
                s.progress = (float) done / (float) total;
                post (s);
            }
        }
        out.flush();
        if (done < 100000) return false;   // not a real archive
    }

    s.state = Status::State::installing;
    post (s);

    // 3) unzip (ditto keeps the bundle structure and permissions)
    juce::ChildProcess unzip;
    if (! unzip.start (juce::StringArray { "/usr/bin/ditto", "-x", "-k", zip.getFullPathName(), tmp.getFullPathName() }))
        return false;
    unzip.waitForProcessToFinish (60000);
    auto newApp = tmp.getChildFile ("Brume").getChildFile ("Brume.app");
    if (! newApp.isDirectory()) return false;

    // 4) install while Brume is still running (macOS may kill helper processes when the app quits):
    //    plugins into the user folder, then swap the app bundle (a running app can be replaced on macOS)
    auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    if (! app.getFullPathName().endsWith (".app")) return false;
    const auto q = [] (const juce::File& f) { return f.getFullPathName().quoted(); };
    auto src = tmp.getChildFile ("Brume");
    juce::String script;
    script << "#!/bin/sh\n"
           << "exec 2>&1\n"
           << "set -x\n"
           << "P=\"$HOME/Library/Audio/Plug-Ins\"\n"
           << "mkdir -p \"$P/VST3\" \"$P/Components\"\n"
           << "rm -rf \"$P/VST3/Brume.vst3\"; /usr/bin/ditto " << q (src.getChildFile ("Brume.vst3")) << " \"$P/VST3/Brume.vst3\"\n"
           << "rm -rf \"$P/Components/Brume.component\"; /usr/bin/ditto " << q (src.getChildFile ("Brume.component")) << " \"$P/Components/Brume.component\"\n"
           << "/usr/bin/xattr -dr com.apple.quarantine \"$P/VST3/Brume.vst3\" \"$P/Components/Brume.component\"\n"
           << "APP=" << q (app) << "\n"
           << "rm -rf \"$APP.new\"\n"
           << "if /usr/bin/ditto " << q (newApp) << " \"$APP.new\" && rm -rf \"$APP\" && mv \"$APP.new\" \"$APP\"; then\n"
           << "  /usr/bin/xattr -dr com.apple.quarantine \"$APP\"\n"
           << "  echo UPDATE_OK\n"
           << "else\n"
           << "  echo UPDATE_FAILED\n"
           << "  rm -rf \"$APP.new\"\n"
           << "fi\n"
           << "exit 0\n";
    auto scriptFile = tmp.getChildFile ("install.sh");
    if (! scriptFile.replaceWithText (script)) return false;

    juce::ChildProcess installer;
    if (! installer.start (juce::StringArray { "/bin/sh", scriptFile.getFullPathName() }))
        return false;
    const auto output = installer.readAllProcessOutput();
    installer.waitForProcessToFinish (120000);

    auto log = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/Brume-update.log");
    log.getParentDirectory().createDirectory();
    log.replaceWithText (juce::Time::getCurrentTime().toString (true, true) + "\n" + output.replace ("UPDATE_FAILED", "update failed"));

    if (! output.contains ("UPDATE_OK"))
    {
        newApp.revealToUser();   // fallback: the new app is shown in the Finder, ready to drag into Applications
        Status f;
        f.state = Status::State::failed;
        f.message = fr ("macOS a bloqué le remplacement : glisse la nouvelle Brume.app (ouverte dans le Finder) dans Applications.");
        post (f);
        return true;   // handled: do not quit
    }

    // 5) relaunch the new version, then quit this one
    juce::ChildProcess opener;
    opener.start (juce::StringArray { "/usr/bin/open", "-n", app.getFullPathName() });
    opener.waitForProcessToFinish (10000);
    juce::MessageManager::callAsync ([] { juce::JUCEApplicationBase::quit(); });
    return true;
   #endif
}

//==============================================================================
UpdateBanner::UpdateBanner()
{
    setVisible (false);
    button.setButtonText (fr ("Mettre à jour"));
    button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffffb547));
    button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff191d20));
    button.onClick = [this] { button.setEnabled (false); updater.installInBackground(); };
    addAndMakeVisible (button);

    updater.onStatus = [this] (const Updater::Status& s)
    {
        status = s;
        setVisible (s.state != Updater::Status::State::idle);
        button.setVisible (s.state == Updater::Status::State::available
                           || (s.state == Updater::Status::State::failed && s.latestVersion.isNotEmpty()));
        button.setEnabled (true);
        if (s.state == Updater::Status::State::failed) button.setButtonText (fr ("Réessayer"));
        repaint();
    };
    updater.checkInBackground();
}

void UpdateBanner::resized()
{
    button.setBounds (getLocalBounds().removeFromRight (130).reduced (6, 6));
}

void UpdateBanner::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xf0191d20));
    g.setColour (juce::Colour (0xffffb547));
    g.drawRect (getLocalBounds().removeFromBottom (1));

    juce::String text;
    using S = Updater::Status::State;
    switch (status.state)
    {
        case S::available:   text = fr ("Nouvelle version ") + status.latestVersion + fr (" disponible (vous avez v") + Updater::currentVersion() + ")"; break;
        case S::downloading: text = fr ("Téléchargement de ") + status.latestVersion + "... " + juce::String (juce::roundToInt (status.progress * 100.0f)) + " %"; break;
        case S::installing:  text = fr ("Installation... Brume va redémarrer."); break;
        case S::failed:      text = status.message; break;
        case S::idle:        break;
    }
    g.setColour (juce::Colour (0xffece6d6));
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawFittedText (text, getLocalBounds().reduced (12, 0).withTrimmedRight (button.isVisible() ? 130 : 0), juce::Justification::centredLeft, 2);
}
