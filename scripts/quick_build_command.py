import os
import shutil
import subprocess
from textwrap import dedent
from west.commands import WestCommand

class QuickBuildCommand(WestCommand):

    valid_targets = ('app', 'bootloader', 'menuconfig')

    def __init__(self):
        super().__init__(
            name='quick-build',
            help='Shortcut to building stuff quickly',
            description=dedent('''
            A multi-line description of my-command.

            You can split this up into multiple paragraphs and they'll get
            reflowed for you. You can also pass
            formatter_class=argparse.RawDescriptionHelpFormatter when calling
            parser_adder.add_parser() below if you want to keep your line
            endings.'''))

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name,
                                         help=self.help,
                                         description=self.description)

        parser.add_argument('target', help='target to build')
        parser.add_argument('-c', '--clean', action='store_true', help='remove build file before building')

        return parser

    def do_run(self, args, unknown_args):
        
        target = args.target
        clean = args.clean

        # confirm that target is valid
        if target not in QuickBuildCommand.valid_targets:
            valid_targets_string = ', '.join(QuickBuildCommand.valid_targets)
            self.die(f'Invalid target. Valid targets are: {valid_targets_string}')

        # if clean is set, delete the build directory
        if target == 'app' or target == 'menuconfig':
            build_dir = os.path.join(self.manifest.topdir, 'build-app')
            source_dir = os.path.join(self.manifest.topdir, 'temperature-logger/app')
        elif target == 'bootloader':
            build_dir = os.path.join(self.manifest.topdir, f'build-bootloader')
            source_dir = os.path.join(self.manifest.topdir, f'bootloader/mcuboot/boot/zephyr')

        if clean and os.path.exists(build_dir):
            try:
                shutil.rmtree(build_dir)
                self.inf(f"Cleaning build directory: {build_dir}")
            except OSError as e:
                self.die(f'Failed to clean build directory: {e.strerror}')

        # if build directory does not exist, create it
        os.makedirs(build_dir, exist_ok=True)

        # get commnad
        if target == 'app':
            command = f'west build -b esp32_devkitc/esp32/procpu -d {build_dir} {source_dir} -- -DDTC_OVERLAY_FILE="boards/esp32-overlay.dts"'
        elif target == 'bootloader':
            command = f'west build -b esp32_devkitc/esp32/procpu -d {build_dir} {source_dir}'
        elif target == 'menuconfig':
            command = f'west build -b esp32_devkitc/esp32/procpu -d {build_dir} -t menuconfig {source_dir}'
        else:
            self.die("Target not handled.")

        self.inf(f"Running command: {command}")
        subprocess.run(command, shell=True)

