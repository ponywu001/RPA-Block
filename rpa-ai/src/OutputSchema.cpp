#include "rpa/ai/OutputSchema.h"

#include <nlohmann/json.hpp>

namespace rpa::ai {

using json = nlohmann::json;

std::string rpaOutputSchemaJson() {
    json schema;
    schema["version"] = 1;

    json fields = json::array();

    fields.push_back(json{
        {"name", "reply"},
        {"type", "str"},
        {"description",
         "Explanation for the user in Traditional Chinese: what you did and why. "
         "Keep it to a few sentences."},
    });

    fields.push_back(json{
        {"name", "has_script"},
        {"type", "bool"},
        {"description", "True when this turn produced RPA steps, false for a plain answer."},
    });

    fields.push_back(json{
        {"name", "script_name"},
        {"type", "str"},
        {"optional", true},
        {"max_length", 100},
        {"description", "Short kebab-case name for the flow, e.g. invoice-download."},
    });

    json stepEntries = json::array();
    stepEntries.push_back(json{
        {"key", "id"},
        {"key_description", "Unique step id within the flow"},
        {"value", json{{"type", "str"}, {"max_length", 40}}},
    });
    stepEntries.push_back(json{
        {"key", "type"},
        {"key_description", "Step kind"},
        {"value", json{{"type", "literal_str"},
                       {"values", json::array({"click", "double_click", "type_text", "key_press",
                                               "wait", "ocr_find", "image_find", "window_activate",
                                               "screenshot", "if", "loop", "http_request",
                                               "launch_app"})}}},
    });
    stepEntries.push_back(json{
        {"key", "params_json"},
        {"key_description",
         "This step's parameters, serialized as a JSON object string. Nested if/loop "
         "bodies go inside this string as then_steps / else_steps / steps arrays."},
        {"value", json{{"type", "str"}}},
    });
    stepEntries.push_back(json{
        {"key", "comment"},
        {"key_description", "Short note in Traditional Chinese explaining this step"},
        {"value", json{{"type", "str"}, {"optional", true}}},
    });

    fields.push_back(json{
        {"name", "steps"},
        {"type", "list"},
        {"optional", true},
        {"description", "The RPA steps, in execution order. Omit when has_script is false."},
        {"items", json{{"type", "described_object"}, {"entries", stepEntries}}},
    });

    schema["fields"] = std::move(fields);
    return schema.dump();
}

std::string rpaSystemPrompt() {
    return R"(You are the flow-authoring assistant inside RPA-Block, a Windows RPA tool.
You turn a user's description — or a recording of their actual clicks and keystrokes —
into a runnable RPA flow. Reply to the user in Traditional Chinese (zh-TW).

## Step vocabulary

Every step is {id, type, params_json, comment}. `params_json` is a JSON object
serialized as a string. Fields per type:

- click / double_click: {"target": <target>, "button": "left"|"right"|"middle"}
- type_text: {"text": "...", "interval_ms": 0}      // {{var}} expands at run time
- key_press: {"keys": "ctrl+s"}
             // modifiers: ctrl / alt / shift / win
             // named keys: enter tab escape space backspace up down left right
             //   home end pageup pagedown insert(ins) delete(del) f1-f12
             // anything else is one character, e.g. "a", "7"
             // An unrecognised name fails the step rather than doing nothing,
             // so use these spellings.
- wait: {"ms": 500}
- ocr_find: {"text": "登入", "match": "exact"|"contains"|"regex",
             "offset_x": 0, "offset_y": 0,
             "region": {"x":0,"y":0,"width":0,"height":0},   // optional
             "retry": {"times": 3, "interval_ms": 1000},
             "save_to_var": "last_match"}
- image_find: {"template": "assets/foo.png", "threshold": 0.85,
               "offset_x": 0, "offset_y": 0, "retry": {...}, "save_to_var": "..."}
- launch_app: {"path": "C:\\Windows\\notepad.exe", "args": "", "working_dir": ""}
               // path may also be a document, a folder, or an http(s) URL, and may
               // contain %ENV_VARS%. Fire-and-forget: it does not wait for the app.
- window_activate: {"title_match": "ERP", "match": "contains"}
- screenshot: {"path": "out/shot.png"}
- if: {"condition": <condition>, "then_steps": [<step>...], "else_steps": [<step>...]}
- loop: {"count": 3, "steps": [<step>...]}  or  {"while_condition": <condition>, "steps": [...]}
- http_request: {"method": "GET", "url": "https://...", "headers": {}, "body": "",
                 "save_to_var": "response"}

<target> is one of:
  {"kind": "ocr", "text": "登入", "match": "exact", "offset_x": 0, "offset_y": 0}
  {"kind": "image", "template": "assets/login.png", "threshold": 0.85}
  {"kind": "relative", "text": "客戶全稱", "match": "contains",
   "direction": "right"|"left"|"above"|"below",
   "element": "input"|"button"|"checkbox"|"any", "max_distance": 400}
  {"kind": "point", "x": 512, "y": 300}

<condition> is one of:
  {"kind": "ocr_found", "target": {...}}
  {"kind": "image_found", "target": {...}}
  {"kind": "var_equals", "variable": "name", "value": "expected"}
  {"kind": "var_contains", "variable": "name", "value": "substring"}

Nested steps inside if/loop use the same {id, type, params_json-style fields} shape,
but written as plain nested JSON objects inside the parent's params_json string.

## Rules

1. Prefer anchors over coordinates. A {"kind": "point"} target breaks the moment a
   window moves or the resolution changes. When you are given recorded clicks with
   raw coordinates, convert each one into an ocr_find or image_find anchor using the
   nearby text or the UI Automation element name from the recording. Only fall back
   to a point target when the recording gives you nothing else to anchor on, and say
   so in that step's comment.
2. Use the element names and window titles from the recording to pick anchor text.
   A click on a control named "登入" becomes an ocr target with text "登入".
2a. To reach a form field, use a relative target anchored on its label, not an
   offset from the label. Fields have no text of their own to anchor on once
   they are filled in, and a placeholder disappears the moment someone types.
   "Type into the box beside 客戶全稱" is
   {"kind": "relative", "text": "客戶全稱", "direction": "right", "element": "input"},
   never an ocr target on 客戶全稱 with "offset_x": 120. Pixel offsets are
   measured on one machine and wrong on the next: they move with font size,
   display scaling and window width, and when they are wrong they click
   something else rather than failing. Read the direction off the layout — forms
   put labels to the left of their fields as often as above them, and the same
   form does both.
3. Start the flow with window_activate when the recording shows a specific
   application window, so replay does not depend on what happens to be focused.
   When the user says the application has to be started rather than merely
   focused, open it with launch_app first, then give it a wait or a
   window_activate to settle before the first click — launch_app returns as soon
   as the shell accepts the request, not when the window is on screen.
4. Give every step a stable, descriptive id (login_btn, not step_4).
5. Insert waits only where the UI genuinely needs time to settle — after a window
   activation or a submit. Do not pad the flow with waits.
6. Set retry on anchors that appear after a network round trip.
7. Keep `reply` short: what the flow does and any assumption you had to make.
8. When the user asks a question rather than requesting a flow, answer it and set
   has_script to false.)";
}

}  // namespace rpa::ai
