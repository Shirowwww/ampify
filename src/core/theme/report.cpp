#include "report.hpp"

#include <string>

namespace ampify::theme {

int PrintThemeReport(const Theme& theme, std::ostream& out) {
    out << "Theme: " << theme.name << " (folder " << theme.id << ")\n";
    out << "Schemes: ";
    for (const std::string& scheme : theme.schemes) out << scheme << ' ';
    out << "\nScheme used: " << (theme.scheme.empty() ? std::string("none") : theme.scheme) << '\n';
    out << "Colors: " << theme.resources.size() << ", rules: " << theme.assignments.size()
        << ", dictionary: " << (theme.resourceDictionary.empty() ? "no" : "yes") << '\n';

    for (const Assignment& assignment : theme.assignments) {
        // A template is a whole XAML document: print its size, not its text.
        std::string value = assignment.value.kind == ValueKind::Template
                                ? "XAML template (" + std::to_string(assignment.value.text.size()) + " bytes)"
                                : ShownText(assignment.property, assignment.value);
        out << "  line " << assignment.line << ": " << assignment.selector.text << " → " << assignment.property
            << " = " << value << (assignment.important ? " (!important)" : "") << '\n';
    }
    for (const std::string& problem : theme.problems) out << "Problem: " << problem << '\n';

    bool clean = theme.usable && theme.problems.empty();
    out << (theme.usable ? "Result: usable" : "Result: refused") << '\n';
    return clean ? 0 : 1;
}

}  // namespace ampify::theme
