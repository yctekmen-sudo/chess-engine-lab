import { it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/react";
import { NewGame, EnginePanel, Experiments } from "../src/main";
it("requires explicit trust before native executable inspection", () => {
  render(<EnginePanel snapshot={null} command={vi.fn()} />);
  fireEvent.change(screen.getByLabelText("Absolute executable path"), {
    target: { value: "/trusted/engine" },
  });
  expect(
    screen.getByRole("button", { name: "Inspect and save profile" }),
  ).toBeDisabled();
  fireEvent.click(
    screen.getByLabelText(
      "I trust this executable and configured native modules.",
    ),
  );
  expect(
    screen.getByRole("button", { name: "Inspect and save profile" }),
  ).toBeEnabled();
});

function experimentForm() {
  const command = vi.fn();
  render(<Experiments snapshot={{profiles: {a: {id:"a",label:"Chess Lab"},b: {id:"b",label:"Stockfish"}}, experiments:{}} as any} command={command} download={vi.fn()} review={vi.fn()} />);
  fireEvent.change(screen.getByLabelText("Engine A"), {target:{value:"a"}});
  fireEvent.change(screen.getByLabelText("Engine B"), {target:{value:"b"}});
  return {command,create:()=>fireEvent.click(screen.getByRole("button",{name:"Create paired experiment"})),json:()=>JSON.parse((screen.getByLabelText("Match conditions JSON") as HTMLTextAreaElement).value)};
}

it("keeps shared JSON conditions and diagnostics disabled by default", () => {
  const f=experimentForm();f.create();
  expect(f.command).toHaveBeenCalledWith("experiment.create",{white:"a",black:"b",config:f.json()});
  expect(f.json().engineLimits).toBeUndefined();expect(f.json().uciDiagnostics).toBeUndefined();
});

it("assigns per-engine depths, synchronizes JSON and supports mixed movetime", () => {
  const f=experimentForm();fireEvent.click(screen.getByLabelText("Use independent engine limits"));
  fireEvent.change(screen.getByLabelText("Engine A depth"),{target:{value:"8"}});
  fireEvent.change(screen.getByLabelText("Engine B depth"),{target:{value:"3"}});f.create();
  expect(f.json().engineLimits).toEqual([{kind:"depth",depth:8},{kind:"depth",depth:3}]);
  expect(f.command.mock.calls.at(-1)?.[1].config.engineLimits).toEqual(f.json().engineLimits);
  fireEvent.change(screen.getByLabelText("Engine B limit mode"),{target:{value:"movetime"}});
  fireEvent.change(screen.getByLabelText("Engine B movetime (ms)"),{target:{value:"500"}});
  fireEvent.click(screen.getByLabelText("Capture bounded UCI protocol evidence"));f.create();
  expect(f.command.mock.calls.at(-1)?.[1].config.engineLimits).toEqual([{kind:"depth",depth:8},{kind:"movetime",ms:500}]);
  expect(f.json().uciDiagnostics).toEqual({});
  fireEvent.click(screen.getByLabelText("Use independent engine limits"));f.create();
  expect(f.command.mock.calls.at(-1)?.[1].config.engineLimits).toBeUndefined();
  expect(f.json().limit).toEqual({kind:"depth",depth:1});
});

it("loads per-engine JSON limits into controls and rejects invalid depth, movetime and clocks", () => {
  const f=experimentForm();const config={limit:{kind:"depth",depth:1},engineLimits:[{kind:"depth",depth:8},{kind:"movetime",ms:20}],time:{kind:"untimed"}};
  fireEvent.change(screen.getByLabelText("Match conditions JSON"),{target:{value:JSON.stringify(config)}});
  expect(screen.getByLabelText("Engine A depth")).toHaveValue(8);expect(screen.getByLabelText("Engine B movetime (ms)")).toHaveValue(20);
  fireEvent.change(screen.getByLabelText("Engine A depth"),{target:{value:"65"}});f.create();expect(f.command).not.toHaveBeenCalled();expect(screen.getByRole("alert")).toHaveTextContent("Engine A: depth must be an integer from 1 to 64");
  fireEvent.change(screen.getByLabelText("Engine A depth"),{target:{value:"8"}});
  fireEvent.change(screen.getByLabelText("Engine B movetime (ms)"),{target:{value:"-1"}});f.create();expect(f.command).not.toHaveBeenCalled();expect(screen.getByRole("alert")).toHaveTextContent("Engine B: movetime must be an integer");
  fireEvent.change(screen.getByLabelText("Engine B limit mode"),{target:{value:"clock"}});f.create();expect(screen.getByRole("alert")).toHaveTextContent("requires Fischer time");
  fireEvent.change(screen.getByLabelText("Match conditions JSON"),{target:{value:"{invalid"}});f.create();expect(screen.getByRole("alert")).toHaveTextContent("must be valid JSON");
});
it("directs engine versus engine away from casual play", () => {
  const create = vi.fn();
  render(
    <NewGame
      profiles={[{ id: "engine", label: "Engine" }]}
      onCreate={create}
    />,
  );
  fireEvent.change(screen.getByLabelText("White player"), {
    target: { value: "engine" },
  });
  fireEvent.change(screen.getByLabelText("Black player"), {
    target: { value: "engine" },
  });
  expect(screen.getByRole("button", { name: "Create game" })).toBeDisabled();
  fireEvent.change(screen.getByLabelText("White player"), {
    target: { value: "human" },
  });
  fireEvent.click(screen.getByRole("button", { name: "Create game" }));
  expect(create).toHaveBeenCalledWith({
    players: { w: "human", b: "engine" },
    time: { kind: "untimed" },
  });
});

it("accepts a trusted Windows executable path and delegates absolute-path validation to the controller", () => {
  const command = vi.fn();
  render(<EnginePanel snapshot={null} command={command} />);
  const path = "C:\\chess-lab\\chess_lab_uci.exe";
  fireEvent.change(screen.getByLabelText("Absolute executable path"), {
    target: { value: path },
  });
  fireEvent.click(
    screen.getByLabelText(
      "I trust this executable and configured native modules.",
    ),
  );
  const save = screen.getByRole("button", { name: "Inspect and save profile" });
  expect(save).toBeEnabled();
  fireEvent.click(save);
  expect(command).toHaveBeenCalledWith(
    "engine.register",
    expect.objectContaining({
      trust: true,
      config: expect.objectContaining({ path }),
    }),
    undefined,
  );
});
