using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

// Explicit, bounded observation of physical keys in one game process.
internal sealed class InputStudy : ApplicationContext
{
    readonly int targetPid;
    readonly string folder;
    readonly StreamWriter output;
    readonly Stopwatch clock = Stopwatch.StartNew();
    readonly Timer timer = new Timer { Interval = 100 };
    readonly HashSet<int> held = new HashSet<int>();
    readonly Native.Hook callback;
    IntPtr hook;
    long start = -1, lastStatus = -1000;
    bool focused, finished;
    int events;
    bool InGame() { uint pid; Native.GetWindowThreadProcessId(Native.GetForegroundWindow(), out pid); return pid == targetPid; }
    internal InputStudy(int pid, string directory)
    {
        targetPid = pid; folder = Path.GetFullPath(directory); Directory.CreateDirectory(folder);
        output = new StreamWriter(Path.Combine(folder, "events.csv"), false, new UTF8Encoding(false));
        output.AutoFlush = true; output.WriteLine("ms,event,vk");
        callback = OnKey;
        hook = Native.SetWindowsHookEx(13, callback, Native.GetModuleHandle(null), 0);
        if (hook == IntPtr.Zero) { output.Dispose(); throw new Exception("Keyboard hook error " + Marshal.GetLastWin32Error()); }
        Status("Waiting for first physical game key; F10 stops; timeout 90 seconds.");
        timer.Tick += Tick; timer.Start();
    }
    void Status(string message) { File.WriteAllText(Path.Combine(folder,"status.txt"),message); }
    void Tick(object sender, EventArgs args)
    {
        try {
            if (File.Exists(Path.Combine(folder,"stop.request")) || Native.GetAsyncKeyState(121) < 0) { Finish("stopped"); return; }
            long now = clock.ElapsedMilliseconds;
            if (start < 0) { if(now >= 90000) Finish("no physical game key received"); return; }
            long t = now-start;
            if (t >= 300000) { Finish("five minutes complete"); return; }
            bool active = InGame();
            if (active != focused) {
                output.WriteLine(t + "," + (active ? "focus" : "blur") + ",0");
                if (!active) { foreach(int k in held) output.WriteLine(t+",cancel,"+k); held.Clear(); }
                focused = active;
            }
            if(now-lastStatus >= 1000) { Status("Recording; elapsed_ms="+t+"; events="+events+"; game_foreground="+active); lastStatus=now; }
        } catch(Exception e) { Finish("error: "+e.Message); }
    }
    IntPtr OnKey(int code, IntPtr message, IntPtr data)
    {
        try {
            if(code >= 0 && !finished && InGame()) {
                var key=(Native.Key)Marshal.PtrToStructure(data,typeof(Native.Key));
                int msg=message.ToInt32(); bool down=msg==0x100 || msg==0x104;
                if((key.Flags & 0x10)==0 && key.Vk!=121 && (down || msg==0x101 || msg==0x105)) {
                    if(start<0 && down) { start=clock.ElapsedMilliseconds; focused=true; output.WriteLine("0,focus,0"); }
                    if(start>=0 && clock.ElapsedMilliseconds-start<300000) {
                        bool changed=down ? held.Add((int)key.Vk) : held.Remove((int)key.Vk);
                        if(changed) { output.WriteLine((clock.ElapsedMilliseconds-start)+","+(down?"down":"up")+","+key.Vk); events++; }
                    }
                }
            }
        } catch { }
        return Native.CallNextHookEx(hook,code,message,data);
    }
    void Finish(string reason)
    {
        if(finished) return; finished=true; timer.Stop();
        Native.UnhookWindowsHookEx(hook); hook=IntPtr.Zero;
        long elapsed=start<0 ? 0 : clock.ElapsedMilliseconds-start;
        foreach(int k in held) output.WriteLine(elapsed+",cancel,"+k);
        output.Dispose(); timer.Dispose();
        Status("Complete; reason="+reason+"; elapsed_ms="+elapsed+"; events="+events);
        ExitThread();
    }
}
