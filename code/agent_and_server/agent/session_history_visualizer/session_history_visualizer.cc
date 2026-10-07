#include "agent/session_history_visualizer/session_history_visualizer.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace jiaolong {

namespace {

std::string EscapeHtml(const std::string& input) {
  std::string escaped;
  escaped.reserve(input.size());
  for (const char c : input) {
    switch (c) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&#39;";
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}

// Reformats a persisted creation timestamp (e.g. "20260811_120956") as
// "YYYY-MM-DD HH:mm:SS" for display. When the input does not match the
// expected shape, it is returned unchanged.
std::string FormatCreationTimestamp(const std::string& timestamp) {
  if (timestamp.size() == 15 && timestamp[8] == '_') {
    std::tm parsed_time = {};
    std::istringstream input(timestamp);
    input >> std::get_time(&parsed_time, "%Y%m%d_%H%M%S");
    if (!input.fail()) {
      std::ostringstream output;
      output << std::put_time(&parsed_time, "%Y-%m-%d %H:%M:%S");
      return output.str();
    }
  }
  return timestamp;
}

// Renders message content which can be either a plain string or an array of
// content parts (OpenAI style).
std::string RenderMessageContent(const nlohmann::json& content) {
  if (content.is_string()) {
    return EscapeHtml(content.get<std::string>());
  }

  if (content.is_array()) {
    std::string html;
    for (const nlohmann::json& part : content) {
      if (part.is_object() && part.contains("type")) {
        const std::string type = part["type"].get<std::string>();
        if (type == "text" && part.contains("text")) {
          html += "<p>" + EscapeHtml(part["text"].get<std::string>()) + "</p>";
        } else {
          html += "<p>[" + EscapeHtml(type) + "]</p>";
        }
      } else {
        html += "<p>" + EscapeHtml(part.dump()) + "</p>";
      }
    }
    return html;
  }

  if (content.is_null()) {
    return "";
  }

  return EscapeHtml(content.dump());
}

// Renders reasoning content which is usually a plain string produced by the
// LLM (assistant) before the final answer.
std::string RenderReasoningContent(const nlohmann::json& reasoning_content) {
  if (reasoning_content.is_string()) {
    return EscapeHtml(reasoning_content.get<std::string>());
  }

  if (reasoning_content.is_null()) {
    return "";
  }

  return EscapeHtml(reasoning_content.dump());
}

std::string RenderToolCalls(const nlohmann::json& tool_calls) {
  if (!tool_calls.is_array()) {
    return "";
  }

  std::string html;
  for (const nlohmann::json& tool_call : tool_calls) {
    html += "<div class=\"tool-call\">";
    if (tool_call.contains("id")) {
      html += "<div class=\"tool-call-id\">" +
          EscapeHtml(tool_call["id"].get<std::string>()) + "</div>";
    }
    if (tool_call.contains("function")) {
      const nlohmann::json& function = tool_call["function"];
      html += "<div class=\"tool-call-name\">" +
          EscapeHtml(function.value("name", "")) + "</div>";
      if (function.contains("arguments")) {
        const nlohmann::json& arguments = function["arguments"];
        if (arguments.is_string()) {
          // Pretty-print the arguments when they are a JSON string.
          const nlohmann::json parsed_arguments =
              nlohmann::json::parse(arguments.get<std::string>(), nullptr,
                                    false);
          if (!parsed_arguments.is_discarded()) {
            html += "<pre>" + EscapeHtml(parsed_arguments.dump(2)) + "</pre>";
          } else {
            html += "<pre>" + EscapeHtml(arguments.get<std::string>()) +
                "</pre>";
          }
        } else {
          html += "<pre>" + EscapeHtml(arguments.dump(2)) + "</pre>";
        }
      }
    }
    html += "</div>";
  }
  return html;
}

std::string RenderMessage(const nlohmann::json& message) {
  const std::string role = message.value("role", "unknown");

  std::string html;
  html += "<div class=\"message " + role + "\">";
  html += "<div class=\"message-header\">" + EscapeHtml(role) + "</div>";
  if (message.contains("reasoning_content")) {
    const std::string reasoning =
        RenderReasoningContent(message["reasoning_content"]);
    if (!reasoning.empty()) {
      html += "<div class=\"message-reasoning\">";
      html += "<div class=\"reasoning-header\">reasoning</div>";
      html += "<div class=\"reasoning-content\">" + reasoning + "</div>";
      html += "</div>";
    }
  }
  html += "<div class=\"message-content\">";
  if (message.contains("content")) {
    html += RenderMessageContent(message["content"]);
  }
  if (message.contains("tool_call_id")) {
    html += "<div class=\"tool-call-id\">tool_call_id: " +
        EscapeHtml(message["tool_call_id"].get<std::string>()) + "</div>";
  }
  if (message.contains("tool_calls")) {
    html += RenderToolCalls(message["tool_calls"]);
  }
  html += "</div>";
  html += "</div>";
  return html;
}

std::string RenderAgentTurn(const nlohmann::json& agent_turn) {
  const std::string type = agent_turn.value("type", "");

  if (type == "message" && agent_turn.contains("message")) {
    return RenderMessage(agent_turn["message"]);
  }

  if (type == "slash_command") {
    std::string html;
    html += "<div class=\"slash-command\">";
    html += "<div class=\"slash-command-header\">slash command</div>";
    html += "<div class=\"slash-command-content\">" +
        EscapeHtml(agent_turn.value("command", "")) + "</div>";
    html += "</div>";
    return html;
  }

  return "<pre>" + EscapeHtml(agent_turn.dump(2)) + "</pre>";
}

}  // namespace

std::string SessionHistoryVisualizer::GenerateHtml(
    const nlohmann::json& session_history_json) {
  const std::string creation_timestamp = FormatCreationTimestamp(
      session_history_json.value("creationTimestamp", ""));
  const std::string session_id =
      session_history_json.value("sessionId",
          session_history_json.value("sessionID", ""));
  const std::string task_id =
      session_history_json.value("taskId",
          session_history_json.value("taskID", ""));
  const std::string file_location =
      session_history_json.value("fileLocation",
          session_history_json.value("sessionFilePath", ""));

  std::string turns_html;
  if (session_history_json.contains("agentTurns") &&
      session_history_json["agentTurns"].is_array()) {
    for (const nlohmann::json& agent_turn :
        session_history_json["agentTurns"]) {
      turns_html += "<div class=\"turn\">" + RenderAgentTurn(agent_turn) +
          "</div>";
    }
  }

  std::ostringstream html;
  html << R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Jiaolong Session History</title>
<style>
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    margin: 0;
    padding: 24px;
    background-color: #f5f5f5;
    color: #24292f;
  }
  h1 {
    margin: 0 0 8px 0;
    font-size: 24px;
  }
  .session-info {
    margin-bottom: 24px;
    padding: 12px 16px;
    background-color: #ffffff;
    border-radius: 8px;
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.12);
  }
  .session-info-item {
    margin: 4px 0;
    font-size: 14px;
    color: #24292f;
    word-break: break-all;
  }
  .session-info-label {
    display: inline-block;
    min-width: 140px;
    font-weight: 600;
    color: #57606a;
  }
  .session-info-value {
    color: #24292f;
  }
  .turn {
    margin-bottom: 16px;
  }
  .message,
  .slash-command {
    border-radius: 8px;
    overflow: hidden;
    background-color: #ffffff;
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.12);
  }
  .message-header,
  .slash-command-header {
    padding: 6px 12px;
    font-size: 12px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: #ffffff;
  }
  .message.system .message-header { background-color: #6e7781; }
  .message.user .message-header { background-color: #0969da; }
  .message.assistant .message-header { background-color: #1a7f37; }
  .message.tool .message-header { background-color: #8250df; }
  .slash-command-header { background-color: #bf8700; }
  .message-content,
  .slash-command-content {
    padding: 12px;
    background-color: #ffffff;
    font-size: 14px;
    line-height: 1.5;
    white-space: pre-wrap;
    word-break: break-word;
  }
  .message-content p {
    margin: 0 0 8px 0;
  }
  .message-content p:last-child {
    margin-bottom: 0;
  }
  .message-reasoning {
    margin: 12px 12px 12px 12px;
    padding: 8px 12px;
    border-left: 3px solid #bf8700;
    background-color: #fff8e5;
    border-radius: 4px;
  }
  .reasoning-header {
    margin-bottom: 4px;
    font-size: 11px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: #9a6700;
  }
  .reasoning-content {
    font-size: 13px;
    line-height: 1.5;
    color: #4d3d00;
    white-space: pre-wrap;
    word-break: break-word;
  }
  .reasoning-content.collapsed {
    display: -webkit-box;
    -webkit-line-clamp: 2;
    -webkit-box-orient: vertical;
    overflow: hidden;
  }
  .reasoning-expand-toggle {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
    width: 100%;
    margin-top: 8px;
    padding: 6px 12px;
    border: 1px solid #d0d7de;
    border-radius: 6px;
    background-color: #ffffff;
    color: #9a6700;
    font-size: 12px;
    font-weight: 600;
    cursor: pointer;
  }
  .reasoning-expand-toggle:hover {
    background-color: #f6f8fa;
  }
  .reasoning-expand-toggle svg {
    width: 14px;
    height: 14px;
    fill: none;
    stroke: currentColor;
    stroke-width: 1.5;
    stroke-linecap: round;
    stroke-linejoin: round;
    transition: transform 0.2s ease;
  }
  .reasoning-expand-toggle.expanded svg {
    transform: rotate(180deg);
  }
  .tool-call {
    margin-top: 8px;
    padding: 8px;
    border: 1px solid #d0d7de;
    border-radius: 6px;
    background-color: #f6f8fa;
  }
  .tool-call-id {
    font-family: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace;
    font-size: 12px;
    color: #57606a;
  }
  .tool-call-name {
    font-weight: 600;
    margin-top: 4px;
  }
  pre {
    margin: 8px 0 0 0;
    padding: 8px;
    background-color: #24292f;
    color: #e6edf3;
    border-radius: 6px;
    overflow-x: auto;
    font-size: 12px;
    line-height: 1.45;
  }

  /* Tool messages: tall content is collapsed by default and can be expanded. */
  .message.tool .message-content {
    position: relative;
  }
  .message.tool .message-content.collapsed {
    max-height: 120px;
    overflow: hidden;
  }
  .message.tool .message-content.collapsed::before {
    content: "";
    position: absolute;
    left: 0;
    right: 0;
    bottom: 0;
    height: 48px;
    background: linear-gradient(to bottom, rgba(255, 255, 255, 0), #ffffff);
    pointer-events: none;
  }
  .message.tool .message-content.collapsed::after {
    content: "...";
    position: absolute;
    right: 12px;
    bottom: 4px;
    padding: 0 4px;
    background-color: #ffffff;
    color: #57606a;
    font-weight: 600;
    pointer-events: none;
  }
  .tool-expand-toggle {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
    width: 100%;
    padding: 8px 12px;
    border: none;
    border-top: 1px solid #d0d7de;
    background-color: #f6f8fa;
    color: #57606a;
    font-size: 12px;
    font-weight: 600;
    cursor: pointer;
  }
  .tool-expand-toggle:hover {
    background-color: #eaeef2;
  }
  .tool-expand-toggle svg {
    width: 14px;
    height: 14px;
    fill: none;
    stroke: currentColor;
    stroke-width: 1.5;
    stroke-linecap: round;
    stroke-linejoin: round;
    transition: transform 0.2s ease;
  }
  .tool-expand-toggle.expanded svg {
    transform: rotate(180deg);
  }
</style>
</head>
<body>
<h1>Jiaolong Session History</h1>
<div class="session-info">)";
  if (!session_id.empty()) {
    html << R"(
  <div class="session-info-item"><span class="session-info-label">Session ID:</span> <span class="session-info-value">)"
        << EscapeHtml(session_id) << R"(</span></div>)";
  }
  if (!task_id.empty()) {
    html << R"(
  <div class="session-info-item"><span class="session-info-label">Task ID:</span> <span class="session-info-value">)"
        << EscapeHtml(task_id) << R"(</span></div>)";
  }
  if (!file_location.empty()) {
    html << R"(
  <div class="session-info-item"><span class="session-info-label">File location:</span> <span class="session-info-value">)"
        << EscapeHtml(file_location) << R"(</span></div>)";
  }
  html << R"(
  <div class="session-info-item"><span class="session-info-label">Creation timestamp:</span> <span class="session-info-value">)"
      << EscapeHtml(creation_timestamp) << R"(</span></div>
</div>
<div class="turns">)" << turns_html << R"(</div>
<script>
  (function() {
    // Tool message contents taller than this many pixels are collapsed by
    // default and can be expanded by the user.
    var COLLAPSE_THRESHOLD = 160;

    var chevronDown = '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M4.5 6.5l3.5 3.5 3.5-3.5"/></svg>';
    var chevronUp = '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M4.5 9.5l3.5-3.5 3.5 3.5"/></svg>';

    function init() {
      var contents = document.querySelectorAll('.message.tool .message-content');
      for (var i = 0; i < contents.length; i++) {
        let content = contents[i];
        if (content.scrollHeight <= COLLAPSE_THRESHOLD) {
          continue;
        }

        // Start collapsed: only a small portion is shown and ends with dots.
        content.classList.add('collapsed');

        let toggle = document.createElement('button');
        toggle.type = 'button';
        toggle.className = 'tool-expand-toggle';
        toggle.setAttribute('aria-expanded', 'false');
        toggle.innerHTML = chevronDown + '<span>Show more</span>';
        toggle.addEventListener('click', function() {
          var collapsed = content.classList.toggle('collapsed');
          var expanded = !collapsed;
          toggle.setAttribute('aria-expanded', String(expanded));
          toggle.classList.toggle('expanded', expanded);
          toggle.innerHTML = (expanded ? chevronUp : chevronDown) +
              '<span>' + (expanded ? 'Show less' : 'Show more') + '</span>';
        });

        content.parentNode.insertBefore(toggle, content.nextSibling);
      }

      initReasoning();
    }

    // Reasoning content taller than two lines is collapsed by default and can
    // be expanded by the user.
    function initReasoning() {
      var reasoningContents = document.querySelectorAll('.reasoning-content');
      for (var i = 0; i < reasoningContents.length; i++) {
        let content = reasoningContents[i];

        // Measure the full (unclamped) height first.
        var fullHeight = content.scrollHeight;

        // Collapse to two lines and check whether the content overflows.
        content.classList.add('collapsed');
        var collapsedHeight = content.clientHeight;

        // Content fits within two lines: leave it fully expanded.
        if (fullHeight <= collapsedHeight + 1) {
          content.classList.remove('collapsed');
          continue;
        }

        // Content is too long: show an expand button that toggles the clamp.
        let toggle = document.createElement('button');
        toggle.type = 'button';
        toggle.className = 'reasoning-expand-toggle';
        toggle.setAttribute('aria-expanded', 'false');
        toggle.innerHTML = chevronDown + '<span>Show more</span>';
        toggle.addEventListener('click', function() {
          var collapsed = content.classList.toggle('collapsed');
          var expanded = !collapsed;
          toggle.setAttribute('aria-expanded', String(expanded));
          toggle.classList.toggle('expanded', expanded);
          toggle.innerHTML = (expanded ? chevronUp : chevronDown) +
              '<span>' + (expanded ? 'Show less' : 'Show more') + '</span>';
        });

        content.parentNode.insertBefore(toggle, content.nextSibling);
      }
    }

    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', init);
    } else {
      init();
    }
  })();
</script>
</body>
</html>
)";

  return html.str();
}

}  // namespace jiaolong
