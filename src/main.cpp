#include "app.h"

#include <cstdio>
#include <string_view>

int main(int argc, char **argv)
{
    std::string_view testProfile;
    if (argc != 1)
    {
        if (argc != 3 || std::string_view(argv[1]) != "--test-profile")
        {
            std::fputs("Usage: Project_Raidline [--test-profile NAME]\n", stderr);
            return 2;
        }
        testProfile = argv[2];
        if (testProfile.empty() ||
            testProfile.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string_view::npos)
        {
            std::fputs("Invalid test profile name.\n", stderr);
            return 2;
        }
    }
    App app;
    return app.run(testProfile);
}
