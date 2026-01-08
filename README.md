# FireMarshal_mecamem_hotplug

This is a fork of firesim/FireMarshal to support hotplug function for MECA memory.

## Setup

FireMarshal uses the [Conda](https://docs.conda.io/en/latest/) package manager to help manage system dependencies.
This allows users to create an "environment" that holds system dependencies like ``make``, ``git``, etc.

Next you can run the following commands to create a FireMarshal environment called ``firemarshal`` with a RISC-V compatible toolchain:

```bash
./scripts/setup-conda.sh --conda-env-name firemarshal
```

To enter this environment, you then run the ``activate`` command.
**Note that this command should be run whenever you want to use FireMarshal so that packages can be properly be added to your ``PATH``**.

```bash
conda activate firemarshal # or whatever name/prefix you gave during environment creation
```

In addition to standard packages added in the conda environment, you will need the RISC-V ISA simulator (Spike).
To install Spike, please refer to https://github.com/riscv-software-src/riscv-isa-sim.

Finally, if you are running as a user on a machine without ``sudo`` access it is required for you to install ``guestmount`` for disk manipulation.
You can install this through your default package manager (for ex. ``apt`` or ``yum``).
You can also follow along with the ``guestmount`` [installation instructions found in the FireSim project](https://docs.fires.im/en/stable/Getting-Started-Guides/On-Premises-FPGA-Getting-Started/Initial-Setup/RHS-Research-Nitefury-II.html?highlight=guestmount#install-guestmount).

## Basic Usage

Followind is the procedure to generate no-disk image for MECA.

```bash
./init-submodules.sh
```

Patch to fix some errors related to FireMarshal and enable memory hotplug function for MECA memory.

```bash
./fix_error_and_add_mem_probe_riscv.sh
./fix_br_sudo_mknod.sh
```

Building no-disk image:

```bash
./marshal -v -d build br-base.json
```

To run in qemu:

```bash
./marshal -d launch br-base.json
```

To flatten the no-disk image:

```bash
./marshal -v -d install -t prototype br-base.json

ls -alh images/prototype/br-base/*flat
-rwxrwxr-x 1 swsok swsok 32M Jan  8 04:01 images/prototype/br-base/br-base-bin-nodisk-flat
```

To write the image to SD card: (if SD card is /dev/sdc)

```bash
sudo dd if=images/prototype/br-base/br-base-bin-nodisk-flat of=/dev/sdc1 bs=4k
```

## Caution

The default loading image size in the Chipyard design is 30MB. If your generated image exceeds this limit, the boot process will fail.

To resolve this, you need to increase the maximum loading size in the Chipyard configuration. Modify the relevant parameter in your Chipyard design to accommodate the actual image size (e.g., 32MB or larger based on your flattened image size).

Check your generated image size with:

```bash
ls -lh images/prototype/br-base/br-base-bin-nodisk-flat
```

Ensure that the Chipyard `BootROM` or SD card loader configuration supports loading an image of that size before attempting to boot.
