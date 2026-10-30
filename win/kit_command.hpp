#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../core/journal.hpp"
#include "../core/natives/kit.hpp"

namespace amk::win {

// AMK.kit.run on Windows: runs amk.exe, which sits beside amk_kernel.dll, with no window and no input (a question
// gets end of file, so only --yes acts), and reports its exit code and output.
class KitCommandRunner final : public KitCommandHost {
public:
    explicit KitCommandRunner(Journal& journal);

    void RunKitCommand(const std::string& owner, const std::vector<std::string>& arguments, Done done) override;
    void Forget(const std::string& owner) override;

private:
    // Shared with the threads of the runs: each run reports only if its owner's generation did not change.
    struct Owners {
        std::mutex lock;
        std::map<std::string, std::uint64_t> generation;
    };

    std::shared_ptr<Owners> m_owners = std::make_shared<Owners>();
    Journal& m_journal;
};

}  // namespace amk::win
