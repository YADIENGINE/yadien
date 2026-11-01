#pragma once

#include <filesystem>
#include <string>

// Installing an update closes every program running from the install folder without asking (design D15), a build
// started with `<engine> build` included. So a launcher command marks itself as running for as long as it lives, and
// the update waits until no such mark exists. The mark is a named Windows mutex nobody owns: several commands can
// hold it at once, and it disappears with the last process even if that one is killed.
class CommandActivity {
public:
    // `installFolder`: where the executable runs from; marks of another installation do not count.
    explicit CommandActivity(const std::filesystem::path& installFolder);
    ~CommandActivity();

private:
    void* _handle = nullptr;

public:
    CommandActivity(const CommandActivity&) = delete;
    CommandActivity& operator=(const CommandActivity&) = delete;
};

[[nodiscard]] bool CommandsRunning(const std::filesystem::path& installFolder);
