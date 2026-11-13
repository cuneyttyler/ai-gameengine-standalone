// A Paint-method build run as the Build panel runs it, with the real AI service (AIGE_PAINT_BUILD="<request>|<out dir>|<port>"):
// the request through ① Confirm and ② Layout to ③ Plan, in a fresh project in the temp folder. Only the pictures go to
// <out dir>: layout.png (② Layout's zones) and plan.png (③ Plan's map); the project and its log stay in the temp folder.
#include "revise_harness.h"

using namespace aige;
using namespace aige::revise_tests;

TEST(PaintBuild, LayoutAndPlanPictures) {
    const char* arg = std::getenv("AIGE_PAINT_BUILD");
    if (arg == nullptr) GTEST_SKIP() << "set AIGE_PAINT_BUILD=<request>|<out dir>|<port>";
    std::vector<std::string> parts;
    {
        std::stringstream in(arg);
        for (std::string part; std::getline(in, part, '|');) parts.push_back(part);
    }
    ASSERT_EQ(parts.size(), 3u);
    const fs::path out = parts[1];
    const fs::path work = fs::temp_directory_path() / "aige_paint_build";
    LiveBuild run(work, std::stoi(parts[2]), "Paint");
    ASSERT_EQ(run.Error(), "");
    ASSERT_EQ(run.ToLayout(parts[0]), "");
    run.Picture("zones.png", "layout.png");
    ASSERT_EQ(run.ToPlanned(), "");
    run.Picture("layout.png", "plan.png");
    std::error_code ec;
    fs::create_directories(out, ec);
    for (const char* name : {"layout.png", "plan.png"}) {
        ASSERT_TRUE(fs::exists(work / name)) << name;
        fs::copy_file(work / name, out / name, fs::copy_options::overwrite_existing, ec);
    }
    std::cout << "method " << run.Build().State().value("method", "?") << "; pictures in " << out.string() << " (the run's log: "
              << (work / "run.log").string() << ")" << std::endl;
}
