"""Place building's AI calls (development_plan.md 7.4, place-interaction.md
Part B): think, then answer in the strict schema; drafts from several
readings; one repair round. With scripted models."""
import json

from fastapi.testclient import TestClient

from app.main import create_app
from app.places import READINGS, interpret, repair_brief, resolve_mode, think_then_answer
from app.providers import Completion, ScriptedProvider

CONTEXT = {"buildTypes": [{"id": "settlement", "components": ["residential-ward", "garden", "wall"],
                           "scale": [{"class": "village", "buildings": [15, 60]}]}]}


def brief(scale_class="village", count=(20, 30), garden="common", **extra):
    return {"brief": {"buildType": "settlement", "name": "Riverford",
                      "scale": {"class": scale_class, "buildings": {"count": list(count)}},
                      "components": {"residential-ward": {"enabled": True, "count": [1, 2]},
                                     "garden": {"enabled": True, "density": garden}}, **extra},
            "confidence": {"scale.class": 0.9, "components.garden.density": 1.7, "bad": "x"},
            "assumptions": ["  no walls:  the request didn't  mention them "], "questions": [],
            "rationale": "A village by a river."}


def scripted(answer):
    """Thinks in prose when asked for free text; answers with `answer(reading)` when asked for JSON."""
    provider = ScriptedProvider(lambda messages: "")

    def reply(messages):
        user = messages[-1]["content"]
        if len(provider.requests) - 1 in provider.text_requests:
            return "The request says a village by a river. Twenty to thirty houses; gardens are common."
        reading = next((r for r, text in READINGS if text in user), "literal")
        return json.dumps(answer(reading))

    provider._answers = reply
    return provider


def test_two_pass_thinks_in_free_text_then_answers_given_the_thinking():
    provider = scripted(lambda reading: brief())
    step = think_then_answer(provider, "Task.", "Inputs.", "Shape.", "auto")
    assert step.mode == "two-pass"
    assert provider.text_requests == [0]  # the first call is free text, the second JSON
    assert "Twenty to thirty houses" in provider.requests[1][-1]["content"]  # the answer sees the thinking
    assert step.thinking.startswith("The request says")
    assert step.answer["brief"]["name"] == "Riverford"
    assert step.usage.requests == 2


def test_one_pass_and_native_modes():
    one = ScriptedProvider([json.dumps({"thinking": "short thought", **brief()})])
    step = think_then_answer(one, "Task.", "Inputs.", "Shape.", "one-pass")
    assert step.mode == "one-pass" and step.thinking == "short thought" and "thinking" not in step.answer
    assert '"thinking"' in one.requests[0][0]["content"]  # asked for it as the first key

    class Reasoner(ScriptedProvider):
        reasoning = True

        def complete(self, messages):
            done = super().complete(messages)
            return Completion(text=done.text, usage=done.usage, model=done.model, thinking="native thought")

    native = Reasoner([json.dumps(brief())])
    assert resolve_mode(native, "auto") == "native"
    step = think_then_answer(native, "Task.", "Inputs.", "Shape.")
    assert step.mode == "native" and step.thinking == "native thought" and len(native.requests) == 1


def test_answers_wrapped_in_prose_are_still_read():
    provider = ScriptedProvider(["Here it is:\n```json\n" + json.dumps(brief()) + "\n```"])
    assert think_then_answer(provider, "T", "I", "S", "one-pass").answer["brief"]["buildType"] == "settlement"
    assert think_then_answer(ScriptedProvider(["no json"]), "T", "I", "S", "one-pass").errors == ["the answer wasn't JSON"]


def test_drafts_come_from_different_readings_and_are_cleaned():
    answers = {"literal": brief("village", (15, 20), "some"), "typical": brief("village", (25, 35)),
               "generous": brief("large-village", (60, 80), "abundant")}
    provider = scripted(lambda reading: answers[reading])
    result = interpret(provider, "a village by a river", CONTEXT, drafts=3, workers=1)
    assert result["ok"]
    assert [d["reading"] for d in result["drafts"]] == ["literal", "typical", "generous"]
    first = result["drafts"][0]
    assert first["brief"]["schema"] == "aige.brief/1"
    assert first["confidence"] == {"scale.class": 0.9, "components.garden.density": 1.0}  # clamped, junk dropped
    assert first["assumptions"] == ["no walls: the request didn't mention them"]
    assert first["thinking"] and first["mode"] == "two-pass"
    assert result["drafts"][2]["brief"]["scale"]["class"] == "large-village"
    assert result["usage"]["requests"] == 6  # two calls per draft


def test_a_draft_without_a_brief_is_an_error_but_others_survive():
    provider = scripted(lambda reading: {"nothing": True} if reading == "literal" else brief())
    result = interpret(provider, "a village", CONTEXT, drafts=2, workers=1)
    assert result["ok"] and len([d for d in result["drafts"] if "brief" in d]) == 1
    assert any("literal: the answer has no brief" in w for w in result["warnings"])


def test_repair_lists_the_violations_and_answers_without_thinking():
    provider = ScriptedProvider([json.dumps(brief())])
    result = repair_brief(provider, "a village", CONTEXT, brief()["brief"],
                          [{"where": "components.walls", "message": "isn't a component of a settlement"}])
    assert result["ok"] and result["brief"]["name"] == "Riverford"
    assert provider.text_requests == []  # one call, no thinking pass
    assert "components.walls: isn't a component of a settlement" in provider.requests[0][-1]["content"]


def test_the_endpoints():
    provider = scripted(lambda reading: brief())
    client = TestClient(create_app(provider=provider))
    response = client.post("/places/interpret", json={"query": "a village", "context": CONTEXT, "drafts": 2}).json()
    assert response["ok"] and len(response["drafts"]) == 2
    assert client.post("/places/interpret", json={"query": " ", "context": CONTEXT}).json()["ok"] is False
    assert TestClient(create_app()).post("/places/interpret", json={"query": "x", "context": {}}).json()["ok"] is False


# --- Plan (7.5) ------------------------------------------------------------------------

from app.places import plan_root, plan_zone, repair_node  # noqa: E402

ROOT = {"quantity": {"buildings": {"count": 30}}, "quality": "not an object",
        "children": [{"id": "market", "module": "market-ward"}, "junk", {"id": "x"}], "rationale": "r"}


def test_plan_root_thinks_and_cleans_the_answer():
    provider = ScriptedProvider(lambda messages: "two wards" if len(provider.requests) - 1 in provider.text_requests
                                else json.dumps(ROOT))
    result = plan_root(provider, {"buildType": "settlement"}, {"root": {}, "modules": []})
    assert result["ok"] and result["mode"] == "two-pass" and result["thinking"] == "two wards"
    answer = result["answer"]
    assert answer["quality"] == {}  # not an object: emptied
    assert [c["id"] for c in answer["children"]] == ["market"]  # junk and module-less children dropped
    assert "85%" in provider.requests[0][0]["content"]  # the self-check is in the task


def test_plan_zone_and_repair_node():
    zone_answer = {"children": [{"id": "w/cottages", "module": "house-plot", "repeat": 20}]}
    provider = ScriptedProvider([json.dumps(zone_answer)])
    result = plan_zone(provider, {}, {"id": "w", "module": "residential-ward"}, [{"id": "m"}], {}, mode="one-pass")
    assert result["ok"] and result["answer"]["children"][0]["repeat"] == 20
    assert "The other zones" in provider.requests[0][-1]["content"]

    fixer = ScriptedProvider([json.dumps({"quality": {"wealth": "rich"}})])
    fixed = repair_node(fixer, {"id": "w", "quality": {"wealth": "golden"}},
                        [{"where": "w.wealth", "message": "'golden' isn't one of poor, common, rich"}], {})
    assert fixed["ok"] and fixed["answer"]["quality"]["wealth"] == "rich"
    assert fixer.text_requests == []
    assert "w.wealth: 'golden'" in fixer.requests[0][-1]["content"]


def test_plan_endpoints():
    provider = ScriptedProvider(lambda messages: json.dumps(ROOT))
    client = TestClient(create_app(provider=provider))
    assert client.post("/places/plan-root", json={"brief": {}, "context": {}, "mode": "one-pass"}).json()["ok"]
    assert client.post("/places/plan-zone", json={"brief": {}, "zone": {}, "context": {}, "mode": "one-pass"}).json()["ok"]
    assert client.post("/places/repair-node", json={"node": {}, "problems": [], "context": {}}).json()["ok"]
    assert TestClient(create_app()).post("/places/plan-root", json={"brief": {}, "context": {}}).json()["ok"] is False


from app.places import revise_plan  # noqa: E402


def test_revise_plan_keeps_known_changes():
    answer = {"summary": " Houses around the green. ", "brief": "junk",
              "changes": [{"op": "zone", "node": "commons", "children": []}, {"op": "explode"}, "junk",
                          {"op": "set", "node": "merchant-ward", "quantity": {"plots": {"count": 10}}}]}
    provider = ScriptedProvider([json.dumps(answer)])
    result = revise_plan(provider, {}, {"zones": []}, {}, "put houses in the commons", {}, mode="one-pass")
    assert result["ok"]
    assert [c["op"] for c in result["answer"]["changes"]] == ["zone", "set"]
    assert result["answer"]["brief"] == {} and result["answer"]["summary"] == "Houses around the green."
    assert "put houses in the commons" in provider.requests[0][-1]["content"]


def test_revise_endpoint():
    provider = ScriptedProvider(lambda messages: json.dumps({"summary": "ok", "changes": []}))
    client = TestClient(create_app(provider=provider))
    body = {"brief": {}, "plan": {}, "instruction": "more trees", "context": {}, "mode": "one-pass"}
    assert client.post("/places/revise-plan", json=body).json()["ok"]
    assert client.post("/places/revise-plan", json={**body, "instruction": " "}).json()["errors"] == ["Say what to change"]


from app.places import review_layout  # noqa: E402


def test_review_looks_at_the_map_and_proposes_changes():
    answer = {"thinking": "the temple ward has cottages only", "issues": ["no temple in the temple ward", ""],
              "summary": "Adds the temple.", "changes": [{"op": "zone", "node": "temple-ward", "children": []}]}
    provider = ScriptedProvider([json.dumps(answer)])
    image = {"type": "image_url", "image_url": "data:image/png;base64,AAAA"}
    result = review_layout(provider, image, {}, {}, {}, [{"what": "temple", "built": 0}], {})
    assert result["ok"] and result["thinking"] == "the temple ward has cottages only"
    assert result["answer"]["issues"] == ["no temple in the temple ward"]
    assert result["answer"]["changes"][0]["op"] == "zone"
    content = provider.requests[0][-1]["content"]
    assert content[-1] == image and "asked, planned, built" in content[0]["text"]


def test_review_endpoint(tmp_path):
    from PIL import Image
    (tmp_path / "places" / "v").mkdir(parents=True)
    Image.new("RGB", (40, 30), (90, 160, 90)).save(tmp_path / "places" / "v" / "layout.png")
    vision = ScriptedProvider(lambda messages: json.dumps({"issues": [], "summary": "Looks right.", "changes": []}))
    client = TestClient(create_app(provider=vision, vision=vision))
    client.put("/project", json={"root": str(tmp_path)})
    body = {"image": "places/v/layout.png", "brief": {}, "plan": {}, "context": {}}
    reply = client.post("/places/review-layout", json=body).json()
    assert reply["ok"] and reply["answer"]["summary"] == "Looks right."
    assert client.post("/places/review-layout", json={**body, "image": "../outside.png"}).json()["ok"] is False
