import json
import argparse
from rich.console import Console
from rich.panel import Panel
from rich.syntax import Syntax
from rich.markdown import Markdown

console = Console()


def parse_jsonl(file_path, limit=None):
    try:
        with open(file_path, "r") as f:
            for i, line in enumerate(f, 1):
                if limit and i > limit:
                    break

                try:
                    entry = json.loads(line)
                    console.print(f"[bold blue]--- Entry {i} ---[/bold blue]")

                    # 1. Instruction
                    instruction = entry.get("instruction", "N/A")
                    console.print(
                        Panel(
                            instruction,
                            title="[bold green]Instruction[/bold green]",
                            border_style="green",
                        )
                    )

                    # 2. Input (Context + State Before)
                    # The input usually contains "Context: ... \n\nSTATE BEFORE:\n ... code ..."
                    input_text = entry.get("input", "")

                    # Try to separate Context from Code for better rendering
                    if "STATE BEFORE:" in input_text:
                        context_part, code_part = input_text.split("STATE BEFORE:", 1)
                        console.print(
                            Panel(
                                context_part.strip(),
                                title="[bold yellow]Context[/bold yellow]",
                                border_style="yellow",
                            )
                        )

                        # Render State Before as C++ code
                        syntax = Syntax(
                            code_part.strip(), "cpp", theme="monokai", line_numbers=True
                        )
                        console.print(
                            Panel(
                                syntax,
                                title="[bold yellow]State Before (Source)[/bold yellow]",
                                border_style="yellow",
                            )
                        )
                    else:
                        console.print(
                            Panel(
                                input_text,
                                title="[bold yellow]Input[/bold yellow]",
                                border_style="yellow",
                            )
                        )

                    # 3. Output (Thought + Diff)
                    output_text = entry.get("output", "")

                    if "<THOUGHT>" in output_text and "</THOUGHT>" in output_text:
                        parts = output_text.split("</THOUGHT>")
                        thought_content = parts[0].replace("<THOUGHT>", "").strip()
                        diff_content = (
                            parts[1]
                            .replace("<CODE>", "")
                            .replace("</CODE>", "")
                            .strip()
                        )

                        console.print(
                            Panel(
                                Markdown(thought_content),
                                title="[bold magenta]Thought Process[/bold magenta]",
                                border_style="magenta",
                            )
                        )

                        # Render Diff
                        diff_syntax = Syntax(
                            diff_content, "diff", theme="monokai", line_numbers=True
                        )
                        console.print(
                            Panel(
                                diff_syntax,
                                title="[bold cyan]Output Code (Diff)[/bold cyan]",
                                border_style="cyan",
                            )
                        )
                    else:
                        console.print(
                            Panel(
                                output_text,
                                title="[bold cyan]Output[/bold cyan]",
                                border_style="cyan",
                            )
                        )

                    console.print("\n" + "=" * 80 + "\n")

                except json.JSONDecodeError:
                    console.print(
                        f"[bold red]Error decoding JSON on line {i}[/bold red]"
                    )

    except FileNotFoundError:
        console.print(f"[bold red]File not found: {file_path}[/bold red]")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Read readability script for deluge_agentic_data.jsonl"
    )
    parser.add_argument(
        "file",
        help="Path to the .jsonl file",
        nargs="?",
        default="deluge_agentic_data.jsonl",
    )
    parser.add_argument(
        "--limit",
        "-n",
        type=int,
        help="Limit number of entries to display",
        default=None,
    )
    args = parser.parse_args()

    parse_jsonl(args.file, args.limit)
