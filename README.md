# collab-vm-agent
The source code to the Collab VM Agent

# Requirements
* Visual Studio (Community will work). This uses Visual Studio 2015 although you can probably convert it to other Visual Studio versions.

# Compilation 
This guide is step-by-step.

* Clone or download this repository.
* Open "collab-vm-agent.sln" (Note: If you are not running Visual Studio 2015, you will need to edit it the .sln file before launching so it will actually open).
* Go to Project > Properties
* In Configuration Properties > General, set the Platform Toolset to "Visual Studio 2015 - Windows XP (v140_xp)" or something similar.
* In Character Set, select "Use Unicode Character Set".
* If you're compiling the collab-vm-agent: Set the Configuration Type from "Application (*.exe)" to "Dynamic Library (*.dll)"; if you're compiling the collab-vm-agent-loader, Set the Configuration Type from "Dynamic Library (*.dll)" to "Application (*.exe)" if it is set as such.
* Next, go to Configuration Properties > C/C++, and click on the "Additional Include Directories". Click the down arrow and click "Edit".
* Click on the folder, and select the collab-vm-agent folder, or alternatively, type $(SolutionDir)collab-vm-agent
* Click OK.
* IMPORTANT: If you are on Windows 64-bit (x64), the DLL and Loader should be set to x86. To do this, go to Project > Properties, and click on the button that says "Configuration Manager..."
* Set the Active solution platform to x86.
* Click close.
* Make sure the platform is set to "Active(Win32)"
* Click Apply, then click OK.
* Now you should be able to compile the Agent and the Agent loader.

If you want to compile in debug mode, there should be a button on the top left that says "Release" or "Debug" (located near Locate Windows Debugger). Set it to "Debug" to enable Debug mode in both the collab-vm-agent and the collab-vm-agent-loader, which will give you a Command Prompt window with details of everything that is occurring.

# Making the loader work on Windows XP SP1 and below
By default, the Collab VM Agent DLL and Collab VM Agent Loader will only work on Windows XP Service Pack 2 and above. Visual Studio 2015  also only supports compling for Windows XP SP2 and above. However, the Loader will run fine on Windows XP SP1 and below. A special command, however, needs to be ran in order to allow it to run on these operating systems. If you do not plan on running this on Windows XP SP1 or below, I do not recommend doing this.

Minimum version I have tested is Win2000 SP4, but it (should) theoretically work on Windows NT 4, Windows ME, and maybe even 98 and 95 (but don't hold your breath.)

Step by step instructions:

* Open the Command Prompt. Make sure to run it as Administrator.
* Go to C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\bin (64-bit) OR C:\Program Files\Microsoft Visual Studio 14.0\VC\bin.
* Type the following commands in:
* editbin (path to collab-vm-agent-loader exe).exe /SUBSYSTEM:CONSOLE,4.00 /OSVERSION:4.00 

4.00 is for Windows 95. For a full list of all subsystem numbers click [here](http://pastebin.com/raw/Up67Tnj2). You will receive a warning (LNK42441) that it is an invalid subsystem number. Ignore it and close the command prompt.

Now your Agent DLL and executable will be ready for Windows XP SP1 and below. 
