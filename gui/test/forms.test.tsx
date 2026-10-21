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
