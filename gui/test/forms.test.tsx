import { it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/react";
import { NewGame, EnginePanel } from "../src/main";
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
