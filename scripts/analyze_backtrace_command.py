import subprocess
import regex as re
import os
from textwrap import dedent
from west.commands import WestCommand

class AnalyzeBacktraceCommand(WestCommand):

    def __init__(self):
        super().__init__(
            name='analyze-backtrace',
            help='Translates a raw ESP32 backtrace into source code lines',
            description=dedent('''
            Runs the xtensa-esp32-elf-addr2line tool to translate a raw backtrace 
            (Program Counter addresses) into human-readable source file, line number, 
            and function names using the project's ELF file.
            
            It infers the ELF path using the base directory name (e.g., 'app' or 'peripheral').
            '''))

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name,
                                         help=self.help,
                                         description=self.description)
        # RENAMED 'elf' argument to 'target'
        parser.add_argument('target', help='Base name of the build directory (e.g., "app" if the ELF is in build-app/zephyr/zephyr.elf)')
        parser.add_argument('backtrace', help='Backtrace message (e.g., Backtrace:0x... 0x...)')

        return parser

    def do_run(self, args, unknown_args):
        
        target: str = args.target  # Now refers to the target build directory name
        backtrace: str = args.backtrace
        
        # --- MODIFICATION START ---
        
        # 1. CONSTRUCT THE ELF PATH:
        # self.manifest.topdir gives the root of the west workspace.
        # This joins the root, the build directory, and the standard zephyr.elf path.
        elf_path = os.path.join(self.manifest.topdir, f'build-{target}', 'zephyr', 'zephyr.elf')
        
        # 2. Verify the constructed path exists
        if not os.path.exists(elf_path):
             self.err(f"ELF file not found at calculated path: {elf_path}")
             return
             
        # --- MODIFICATION END ---
        
        # 3. Expand the user directory path for the toolchain
        addr2line_path = os.path.expanduser("~/xtensa-esp32-elf/bin/xtensa-esp32-elf-addr2line")
        
        # 4. Extract only the PC addresses (the hex numbers followed by a colon)
        function_addresses = re.findall(r'(0x[0-9a-fA-F]+)(?=:)', backtrace)
        
        if not function_addresses:
            self.err("Could not find any PC addresses (0x...) followed by a colon in the backtrace.")
            self.inf("Expected format: 'Backtrace:0x...:0x... 0x...:0x...'")
            return

        # 5. Construct the command with the PC addresses and best flags
        address_string = " ".join(function_addresses)
        # Use elf_path instead of the simple 'elf' argument
        command = f"{addr2line_path} -e {elf_path} -a -f -i -C -p {address_string}"
        
        self.inf(f"Running command: {command}\n")
        
        # 6. Execute the command
        try:
            subprocess.run(command, shell=True, check=True)
        except subprocess.CalledProcessError as e:
            self.err(f"addr2line command failed with exit code {e.returncode}")
        except FileNotFoundError:
            self.err(f"addr2line tool not found. Check path: {addr2line_path}")