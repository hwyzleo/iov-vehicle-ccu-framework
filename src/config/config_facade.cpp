#include "config.h"
#include "config_path_resolver.h"
#include "config_file_reader.h"
#include "config_yaml_parser.h"
#include "config_merger.h"
#include "config_snapshot_impl.h"
#include "config_validator_registry.h"
#include "config_error_formatter.h"
#include <utility>
#include <vector>

namespace ccu {
namespace fw {
namespace config {

std::shared_ptr<const ConfigSnapshot>
Config::load(std::string_view service, const LoadOptions& options) {
    // 1. Validate service name.
    if (!PathResolver::isValidServiceName(service)) {
        throw ConfigException(
            "CCU-FW-0004",
            ConfigErrorFormatter::format(
                "CCU-FW-0004", "invalid service name", "", "", "service"),
            "", "service");
    }

    // 2. Normalize configRoots and cwd.
    auto roots = PathResolver::normalizeRoots(options.configRoots);
    auto cwd = PathResolver::normalizeCwd(options.cwd);

    // 3. Six-stage resolve (throws 0001 for required-missing / found-but-invalid).
    auto entries = PathResolver::resolveAll(service, roots, cwd);

    // 4. Read + parse each entry in order.
    std::vector<std::pair<ConfigEntry, NodePtr>> layers;
    layers.reserve(entries.size());
    for (const auto& entry : entries) {
        std::string content = FileReader::read(entry.path);  // 0001 on read failure
        NodePtr tree = YamlParser::parse(content, entry.path); // 0002 on parse failure
        layers.emplace_back(entry, tree);
    }

    // 5. Deep merge in entry order.
    NodePtr merged = Merger::mergeAll(layers);

    // 6. Build immutable snapshot.
    auto impl = std::make_shared<detail::SnapshotImpl>(merged);
    std::shared_ptr<const ConfigSnapshot> snapshot(new ConfigSnapshot(impl));

    // 7. Validate before publishing.
    ValidatorRegistry::instance().validateAll(service, *snapshot);

    // 8. Publish.
    return snapshot;
}

void Config::registerValidator(std::string_view service,
                               std::shared_ptr<Validator> validator) {
    ValidatorRegistry::instance().registerServiceValidator(service, std::move(validator));
}

} // namespace config
} // namespace fw
} // namespace ccu
