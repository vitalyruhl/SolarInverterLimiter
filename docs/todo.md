
# ToDo SolarInverterLimiter

Goal: Refactor the codebase to the new ConfigManager structure, then synchronize HomeAssistant configs, then modernize Logging/MQTT modules and structure them for reuse across projects.

Legend:
- [CURRENT] = currently in progress
- [NEXT] = next reasonable step
- [BLOCKED] = waiting for decision/info
- [COMPLETED] = done

## P0 (critical) – Anglicize the project (repo-wide)


- [CURRENT] Ensure English-only repository artifacts
	- Review /docs/* and translate remaining German text to English
	- Review /dev-info/* and translate remaining German text to English (keep technical meaning)
	- Review /src/* and translate remaining German comments/strings to English
	- Replace emojis in repository artifacts with plain text tags like [INFO]/[WARNING]/[ERROR]
	- Ensure new documentation is written in English (chat explanations remain German)

- [NEXT] Rename non-English/typo identifiers (incremental)
	- Replace German variable/function names with English equivalents (update all references)
	- Keep external interfaces stable (MQTT topics/JSON keys) unless explicitly intended

- [NEXT] Align project guidance
	- Keep language policy consistent across README, docs, and .github instructions
	- Remove outdated or misleading usage text (e.g. references to non-existing config_example.h)

## P0 (critical) – ConfigManager refactor (functional, buildable)

- [COMPLETED] ConfigManager refactor aligned to ConfigManager 2.7.6
	- Settings migrated to ConfigOptions API
	- Runtime/WiFi APIs aligned
	- `pio run -e usb` builds successfully
	- Note: No backward compatibility / migration by explicit decision

## P1 (high) – Update HomeAssistant YAML to the new structure

- [NEXT] Check current state
	- Check `docs/HomeAssistant.yaml` and `docs/HomeAssistant_Dashboard.yaml` for syntax/indentation/issues
	- Verify topics/unique IDs still match the current MQTT topics

- [NEXT] Derive topics from code
	- Document publish topic base + derived topics
	- Align sensor names/units/device classes

- [NEXT] Update YAML
	- Update MQTT sensors
	- Optional: adjust dashboard entities to new IDs/topics

- [NEXT] Minimal validation
	- Check YAML against HomeAssistant conventions (no duplicate `unique_id`, correct device_class/unit)

## P2 (medium) – Modernize Logging & mqttManager / make reusable

- [NEXT] Reference review `/dev-info/helpers/*`
	- Which version is newer? Which APIs does it use?
	- Which parts are project-agnostic and can live as a separate module?

- [NEXT] Define target structure (reusable)
	- e.g. `src/modules/logging/*` and `src/modules/mqtt/*` or later `lib/<ModuleName>/` (PlatformIO)
	- Encapsulate dependencies (ConfigManager, SigmaLogger, PubSubClient)

- [NEXT] Migrate in small steps
	- Modernize logging first (consistent log API, levels, format)
	- Then mqttManager (reconnect, last will, publish/subscribe handling)

- [NEXT] Build validation
	- `pio run -e usb`

## P3 (nice-to-have) – Cleanup / consistency

- [NEXT] Make README consistent (language/content)
	- “not ready yet” vs. current version status (`VERSION` in `src/settings.h`)

- [NEXT] Maintain dev-info structure
	- Briefly document relevant findings from the refactor in `dev-info/` or `docs/` (no copy/paste, only lessons learned)
