#pragma once
#include <Arduino.h>

const char WEB_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <title>SolarLimiter Config</title>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <style>
      body {
        font-family: Arial, sans-serif;
        margin: 20px;
      }
      .category {
        margin: 20px 0;
        padding: 15px;
        border: 1px solid #ddd;
        border-radius: 5px;
      }
      h2 {
        color: #4caf50;
        margin-top: 0;
      }
      .setting {
        margin: 10px 0;
        display: flex;
        align-items: center;
      }
      label {
        flex: 1;
        margin-right: 15px;
      }
      input,
      select {
        flex: 2;
        padding: 8px;
        border: 1px solid #ddd;
        border-radius: 4px;
        font-size: 14px;
      }
      button {
        padding: 12px 24px;
        font-size: 1.1rem;
        margin: 10px 5px;
        background-color: #4caf50;
        color: white;
        border: none;
        border-radius: 4px;
        cursor: pointer;
      }
      button.reset {
        background-color: #f44336;
      }
      #status {
        margin: 15px 0;
        padding: 10px;
        display: none;
      }
      #settingsContainer {
        padding: 15px;
        background: #f5f5f5;
        border-radius: 8px;
      }

      .setting {
        margin: 12px 0;
        padding: 8px;
        background: white;
        border-radius: 4px;
        box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
      }
    </style>
  </head>
  <body>
    <h1>SolarLimiter Configuration</h1>
    <div id="status"></div>
    <div id="settingsContainer"></div>

    <button onclick="apply()">apply</button>
    <button onclick="saveSettings()">Save Settings</button>
    <button onclick="resetToDefaults()" class="reset">Reset to Defaults</button>
    <button onclick="handleReboot()">Reboot</button>

    <script>
      const container = document.getElementById("settingsContainer");
      container.innerHTML = "";

      async function loadSettings() {
        try {
          const response = await fetch("/config/json");
          if (!response.ok)
            throw new Error(`HTTP error! status: ${response.status}`);

          const config = await response.json();
          renderSettings(config);
        } catch (error) {
          showStatus("Error: " + error.message, "red");
          console.error("Fetch error:", error);
        }
      }

      // Debug
      function renderSettings(config) {
        console.log("Rendering config:", config);
        const container = document.getElementById("settingsContainer");
        container.innerHTML = "";

        for (const [category, settings] of Object.entries(config)) {
          const categoryDiv = document.createElement("div");
          categoryDiv.className = "category";
          categoryDiv.innerHTML = `<h2>${category.toUpperCase()}</h2>`;

          for (const [key, value] of Object.entries(settings)) {
            const settingDiv = document.createElement("div");
            settingDiv.className = "setting";

            const label = document.createElement("label");
            label.textContent = key.replace(/_/g, " ") + ":";

            const input = createInputField(category, key, value);

            settingDiv.appendChild(label);
            settingDiv.appendChild(input);
            categoryDiv.appendChild(settingDiv);
          }
          container.appendChild(categoryDiv);
        }
      }

      function createInputField(category, key, value) {
        const inputName = `${category}.${key}`;

        // Handle different input types based on key name or value type
        if (key.toLowerCase().includes("pass")) {
          return createPasswordInput(inputName, value);
        } else if (typeof value === "boolean") {
          return createCheckbox(inputName, value);
        } else if (typeof value === "number") {
          return createNumberInput(inputName, value);
        }
        return createTextInput(inputName, value);
      }

      function createTextInput(name, value) {
        const input = document.createElement("input");
        input.type = "text";
        input.name = name;
        input.value = value;
        return input;
      }

      function createPasswordInput(name, value) {
        const input = document.createElement("input");
        input.type = "password";
        input.name = name;
        input.value = value;
        return input;
      }

      function createNumberInput(name, value) {
        const input = document.createElement("input");
        input.type = "number";
        input.name = name;
        input.value = value;
        return input;
      }

      function createCheckbox(name, checked) {
        const input = document.createElement("input");
        input.type = "checkbox";
        input.name = name;
        input.checked = checked;
        return input;
      }

      async function saveSettings() {
        if (confirm("Are you sure you want to save the settings?")) {
          try {
            const config = {};

            document.querySelectorAll("input").forEach((input) => {
              const [category, key] = input.name.split(".");
              if (!config[category]) {
                config[category] = {};
              }
              if (input.type === "checkbox") {
                config[category][key] = input.checked;
              } else if (input.type === "number") {
                config[category][key] = Number(input.value);
              } else {
                config[category][key] = input.value;
              }
            });

            const response = await fetch("/config/save", {
              method: "POST",
              headers: {
                "Content-Type": "application/json",
              },
              body: JSON.stringify(config),
            });

            if (response.ok) {
              showStatus("Settings saved successfully!", "green");
              setTimeout(() => location.reload(), 1000);
            } else {
              showStatus("Error saving settings!", "red");
            }
          } catch (error) {
            showStatus("Error: " + error.message, "red");
          }
        }
      }

      async function resetToDefaults() {
        if (confirm("Are you sure you want to reset to default values?")) {
          try {
            const response = await fetch("/config/reset", { method: "POST" });
            if (response.ok) {
              showStatus("Reset to defaults successful!", "green");
              setTimeout(() => location.reload(), 1000);
            }
          } catch (error) {
            showStatus("Reset failed: " + error.message, "red");
          }
        }
      }

      async function apply() {
        if (confirm("Are you sure you want to apply the settings?")) {
          try {
            const config = {};

            document.querySelectorAll("input").forEach((input) => {
              const [category, key] = input.name.split(".");
              if (!config[category]) {
                config[category] = {};
              }
              if (input.type === "checkbox") {
                config[category][key] = input.checked;
              } else if (input.type === "number") {
                config[category][key] = Number(input.value);
              } else {
                config[category][key] = input.value;
              }
            });

            const response = await fetch("/config/apply", {
              method: "POST",
              headers: {
                "Content-Type": "application/json",
              },
              body: JSON.stringify(config),
            });

            if (response.ok) {
              showStatus("Settings applyed successfully!", "green");
              setTimeout(() => location.reload(), 1000);
            } else {
              showStatus("Error apply settings!", "red");
            }
          } catch (error) {
            showStatus("Error: " + error.message, "red");
          }
        }
      }

      function showStatus(message, color) {
        const statusDiv = document.getElementById("status");
        statusDiv.style.display = "block";
        statusDiv.style.color = color;
        statusDiv.textContent = message;
        setTimeout(() => (statusDiv.style.display = "none"), 3000);
      }

      function handleReboot() {
        if (confirm("Are you sure you want to reboot ESP?")) {
          fetch("/reboot", { method: "POST" })
            .then((response) => {
              if (!response.ok) throw new Error("Reboot failed");
              alert("Rebooting!");
            })
            .catch((error) => {
              showStatus(error.message, "red");
            });
        }
      }

      // Load settings when page loads
      window.onload = loadSettings;
    </script>
  </body>
</html>

)rawliteral";

class WebHTML {
public:
    const char* getWebHTML();
};