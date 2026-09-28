// Windows recorder companion. Recordings are embedded in ordinary AntiMicroX execute actions.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using System.Xml.Linq;

public sealed class RecordedEvent
{
    public int T, Kind, Code, X, Y; // Kind: key down/up, button down/up, wheel, move
}
public sealed class Recording
{
    public int Version = 1, Width, Height, Duration, Repeats = 1, Gap = 1000;
    public string Name = "新录制动作", ProcessName;
    public List<RecordedEvent> Events = new List<RecordedEvent>();
}
public static class RecordingCodec
{
    public static void Validate(Recording r)
    {
        if (r == null || r.Version != 1 || r.Events == null || r.Events.Count == 0 || r.Events.Count > 20000 ||
            r.Width <= 0 || r.Height <= 0 || r.Duration < 0 || r.Duration > 600000 ||
            r.Repeats < 1 || r.Repeats > 1000 || r.Gap < 100 || r.Gap > 60000 || String.IsNullOrWhiteSpace(r.ProcessName))
            throw new Exception("录制数据或循环设置无效。");
        int time = -1;
        var held = new HashSet<int>();
        foreach (var e in r.Events)
        {
            if (e.T < time || e.T > r.Duration || e.Kind < 0 || e.Kind > 5 ||
                (e.Kind <= 1 && (e.Code < 1 || e.Code > 255 || (e.Code >= 119 && e.Code <= 121))) ||
                ((e.Kind == 2 || e.Kind == 3) && (e.Code < 1 || e.Code > 5)) ||
                (e.Kind == 4 && (e.Code < -32768 || e.Code > 32767)) ||
                (e.Kind >= 2 && (e.X < 0 || e.Y < 0 || e.X >= r.Width || e.Y >= r.Height)))
                throw new Exception("录制事件无效。");
            time = e.T;
            if (e.Kind <= 3)
            {
                int key = e.Code + (e.Kind >= 2 ? 256 : 0);
                if ((e.Kind % 2) == 0) { if (!held.Add(key)) throw new Exception("重复按下事件。"); }
                else if (!held.Remove(key)) throw new Exception("未配对的松开事件。");
            }
        }
        if (held.Count != 0) throw new Exception("录制中存在未松开的按键。");
    }
    public static string Encode(Recording r)
    {
        Validate(r);
        byte[] bytes = Encoding.UTF8.GetBytes(new JavaScriptSerializer().Serialize(r));
        using (var output = new MemoryStream())
        {
            using (var zip = new GZipStream(output, CompressionMode.Compress, true)) zip.Write(bytes, 0, bytes.Length);
            string result = Convert.ToBase64String(output.ToArray());
            if (result.Length > 24000) throw new Exception("录制太长，无法装入单个 action。请缩短录制。");
            return result;
        }
    }
    public static Recording Decode(string value)
    {
        if (value.Length > 24000) throw new Exception("录制数据过大。");
        using (var input = new MemoryStream(Convert.FromBase64String(value)))
        using (var zip = new GZipStream(input, CompressionMode.Decompress))
        using (var output = new MemoryStream())
        {
            byte[] buffer = new byte[4096]; int n;
            while ((n = zip.Read(buffer, 0, buffer.Length)) > 0)
            { if (output.Length + n > 2000000) throw new Exception("录制数据过大。"); output.Write(buffer, 0, n); }
            var r = new JavaScriptSerializer().Deserialize<Recording>(Encoding.UTF8.GetString(output.ToArray()));
            Validate(r); return r;
        }
    }
    public static string ActionJson(Recording r, string executable)
    {
        string xml = new XElement("slots", new XElement("slot", new XElement("path", executable),
            new XElement("arguments", "--play " + Encode(r)), new XElement("mode", "execute"))).ToString(SaveOptions.DisableFormatting);
        return new JavaScriptSerializer().Serialize(new {
            schema = 1, id = Guid.NewGuid().ToString(), name = r.Name, tags = "录制,循环", favorite = false,
            description = String.Format("录制 {0} 个事件；{1} 次；间隔 {2} ms；F10 停止。", r.Events.Count, r.Repeats, r.Gap),
            revision = 1, updatedUtc = DateTime.UtcNow.ToString("o"), slotsXml = xml });
    }
}
internal static class Native
{
    internal delegate IntPtr Hook(int code, IntPtr message, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] internal struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] internal struct Rect { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] internal struct Mouse { public Point Pt; public uint Data, Flags, Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Sequential)] internal struct Key { public uint Vk, Scan, Flags, Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Sequential)] internal struct MouseInput { public int X, Y; public uint Data, Flags, Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Sequential)] internal struct KeyInput { public ushort Vk, Scan; public uint Flags, Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Explicit)] internal struct InputUnion { [FieldOffset(0)] public MouseInput Mouse; [FieldOffset(0)] public KeyInput Key; }
    [StructLayout(LayoutKind.Sequential)] internal struct Input { public uint Type; public InputUnion U; }
    [DllImport("user32.dll", SetLastError=true)] internal static extern IntPtr SetWindowsHookEx(int kind, Hook hook, IntPtr module, uint thread);
    [DllImport("user32.dll")] internal static extern bool UnhookWindowsHookEx(IntPtr hook);
    [DllImport("user32.dll")] internal static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr message, IntPtr data);
    [DllImport("kernel32.dll", CharSet=CharSet.Auto)] internal static extern IntPtr GetModuleHandle(string name);
    [DllImport("user32.dll")] internal static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] internal static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] internal static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] internal static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("user32.dll")] internal static extern short GetAsyncKeyState(int key);
    [DllImport("user32.dll")] internal static extern bool RegisterHotKey(IntPtr window, int id, uint modifiers, uint key);
    [DllImport("user32.dll")] internal static extern bool UnregisterHotKey(IntPtr window, int id);
    [DllImport("user32.dll")] internal static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] internal static extern uint SendInput(uint count, Input[] inputs, int size);
    [DllImport("user32.dll")] internal static extern int GetSystemMetrics(int index);
}
public sealed class RecorderForm : Form
{
    readonly TextBox name = new TextBox { Text = "商城送礼", Width = 300 };
    readonly NumericUpDown repeats = new NumericUpDown { Minimum = 1, Maximum = 1000, Value = 1, Width = 90 };
    readonly NumericUpDown gap = new NumericUpDown { Minimum = 100, Maximum = 60000, Value = 1000, Increment = 100, Width = 90 };
    readonly Label status = new Label { AutoSize = false, Height = 80, Dock = DockStyle.Bottom };
    readonly ListBox timeline = new ListBox { Dock = DockStyle.Fill };
    readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer { Interval = 10 };
    readonly Stopwatch clock = new Stopwatch();
    readonly HashSet<int> held = new HashSet<int>();
    readonly string library;
    readonly bool launchedForPlayback;
    readonly EventWaitHandle externalStop = new EventWaitHandle(false, EventResetMode.AutoReset, "Local\\AntiMicroX.ActionRecorder.Stop");
    readonly FlowLayoutPanel controls = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 140 };
    Native.Hook keyHook, mouseHook;
    IntPtr keyboard, mouse, target;
    Recording recording;
    bool capturing, playing, armed, startRecording;
    int index, completed, count, lastMove, lastX, lastY;
    long passStart;
    Native.Point origin;
    Native.Rect bounds;

    public RecorderForm(string library, Recording loaded)
    {
        this.library = library; recording = loaded; launchedForPlayback = loaded != null;
        Text = "AntiMicroX 快速录制"; Width = 720; Height = 520; StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Microsoft YaHei UI", 10);
        controls.Controls.Add(new Label { Text = "动作名称", AutoSize = true }); controls.Controls.Add(name);
        controls.SetFlowBreak(name, true);
        controls.Controls.Add(new Label { Text = "循环次数", AutoSize = true }); controls.Controls.Add(repeats);
        controls.Controls.Add(new Label { Text = "轮间隔 ms", AutoSize = true }); controls.Controls.Add(gap);
        controls.SetFlowBreak(gap, true);
        AddButton("● 录制 F8", delegate { Arm(true, false); });
        AddButton("试跑一次", delegate { Arm(false, true); });
        AddButton("循环运行 F9", delegate { Arm(false, false); });
        AddButton("保存到 Action", Save);
        var stop = new Button { Text = "■ 停止 F10", Dock = DockStyle.Bottom, Height = 38 };
        stop.Click += delegate { Stop("已停止。"); };
        Controls.Add(timeline); Controls.Add(controls); Controls.Add(stop); Controls.Add(status);
        timer.Tick += Tick; timer.Start();
        FormClosing += delegate { Stop("已停止。"); };
        status.Text = "F8 开始 / 结束录制，F9 循环，F10 停止。开始前有 3 秒切换窗口。\n请录制完整一轮并返回起始页面；默认只执行 1 次。";
        if (loaded != null) { name.Text = loaded.Name; repeats.Value = loaded.Repeats; gap.Value = loaded.Gap; ShowTimeline(); }
        Shown += delegate {
            bool okay = true;
            for (int i = 0; i < 3; ++i) okay &= Native.RegisterHotKey(Handle, 80+i, 0x4000, (uint)(119+i));
            if (!okay) status.Text = "部分 F8 / F9 / F10 快捷键已被占用。可使用窗口按钮；运行时仍可按 F10 停止。";
            if (loaded != null) Arm(false, false);
        };
    }
    void AddButton(string text, Action action)
    { var b = new Button { Text = text, AutoSize = true }; b.Click += delegate { action(); }; controls.Controls.Add(b); }
    protected override void WndProc(ref Message m)
    {
        if (m.Msg == 0x312)
        {
            int id = m.WParam.ToInt32();
            if (id == 80) { if (capturing) Stop("录制完成，请保存到 Action。"); else if (!playing && !armed) Arm(true, false); }
            if (id == 81 && !capturing && !playing && !armed) Arm(false, false);
            if (id == 82) Stop("已停止。");
        }
        base.WndProc(ref m);
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing && !IsDisposed) { Stop("已停止。"); timer.Dispose(); externalStop.Dispose();
            if(IsHandleCreated) for (int i=0; i<3; ++i) Native.UnregisterHotKey(Handle, 80+i); }
        base.Dispose(disposing);
    }
    void Arm(bool record, bool once)
    {
        if (capturing || playing || armed) return;
        if (!record && recording == null) { status.Text = "请先录制一轮。"; return; }
        if (record && recording != null && MessageBox.Show(this, "重新录制会替换当前录制内容。继续？", "重新录制", MessageBoxButtons.OKCancel) != DialogResult.OK) return;
        startRecording = record; count = once ? 1 : (int)repeats.Value;
        externalStop.Reset();
        armed = true; controls.Enabled = false; clock.Restart();
        status.Text = "3 秒后开始，请切换到目标窗口。F10 取消。";
    }
    bool ReadTarget()
    {
        target = Native.GetForegroundWindow(); uint pid;
        Native.GetWindowThreadProcessId(target, out pid);
        if (pid == 0 || pid == Process.GetCurrentProcess().Id) throw new Exception("请先切换到游戏窗口。");
        string process = Process.GetProcessById((int)pid).ProcessName;
        origin = new Native.Point();
        if (!Native.GetClientRect(target, out bounds) || !Native.ClientToScreen(target, ref origin)) throw new Exception("无法读取目标窗口。");
        if (bounds.R <= 0 || bounds.B <= 0) throw new Exception("目标窗口不可用。");
        if (startRecording) recording = new Recording { Name = name.Text, Width = bounds.R, Height = bounds.B, ProcessName = process };
        else if (recording.ProcessName != process || recording.Width != bounds.R || recording.Height != bounds.B)
            throw new Exception("请使用录制时的游戏和窗口尺寸。");
        return true;
    }
    bool TargetUnchanged()
    {
        Native.Rect r; var p = new Native.Point();
        return Native.GetForegroundWindow() == target && Native.GetClientRect(target, out r) &&
            Native.ClientToScreen(target, ref p) && r.R == bounds.R && r.B == bounds.B && p.X == origin.X && p.Y == origin.Y;
    }
    void Tick(object sender, EventArgs args)
    {
        try
        {
            if (externalStop.WaitOne(0)) { Stop("已通过 AntiMicroX 停止。"); return; }
            if ((armed || capturing || playing) && Native.GetAsyncKeyState(121) < 0) { Stop("已通过 F10 停止。"); return; }
            if (armed)
            {
                if (clock.ElapsedMilliseconds < 3000) { status.Text = String.Format("{0} 秒后开始，请切换到目标窗口。", 3-clock.ElapsedMilliseconds/1000); return; }
                ReadTarget();
                for (int k = 1; k <= 255; ++k)
                    if (k != 119 && k != 120 && k != 121 && Native.GetAsyncKeyState(k) < 0)
                        throw new Exception("开始时请松开所有鼠标和键盘按键。");
                armed = false; held.Clear(); clock.Restart(); index = completed = lastMove = 0; passStart = 0;
                if (startRecording)
                {
                    keyHook = KeyboardEvent; mouseHook = MouseEvent;
                    keyboard = Native.SetWindowsHookEx(13, keyHook, Native.GetModuleHandle(null), 0);
                    mouse = Native.SetWindowsHookEx(14, mouseHook, Native.GetModuleHandle(null), 0);
                    if (keyboard == IntPtr.Zero || mouse == IntPtr.Zero) throw new Exception("无法开启录制，请检查程序权限。");
                    capturing = true;
                }
                else { RecordingCodec.Validate(recording); playing = true; }
            }
            if (!(capturing || playing)) return;
            if (!TargetUnchanged()) { Stop("目标窗口切换、移动或尺寸变化，已停止。"); return; }
            if (capturing)
            {
                status.Text = String.Format("录制中 · {0:F1} 秒 · {1} 个事件。F8 结束录制，F10 停止。", clock.Elapsed.TotalSeconds, recording.Events.Count);
                if (clock.ElapsedMilliseconds >= 600000 || recording.Events.Count >= 19500) Stop("已达到录制上限。");
                return;
            }
            long elapsed = clock.ElapsedMilliseconds - passStart;
            while (index < recording.Events.Count && recording.Events[index].T <= elapsed)
            {
                if (!TargetUnchanged() || Native.GetAsyncKeyState(121) < 0) { Stop("执行已停止。"); return; }
                if (elapsed - recording.Events[index].T > 500) { Stop("执行延迟过大，已停止，请重新试跑。"); return; }
                var e = recording.Events[index++]; Send(e);
                if (e.Kind <= 3) { int k = e.Code + (e.Kind >= 2 ? 256 : 0); if (e.Kind % 2 == 0) held.Add(k); else held.Remove(k); }
            }
            status.Text = String.Format("循环执行 {0}/{1} · F10 随时停止。", completed+1, count);
            if (elapsed >= recording.Duration && index == recording.Events.Count)
            {
                if (elapsed < recording.Duration + (int)gap.Value && completed + 1 < count) return;
                ++completed;
                if (completed >= count) { Stop(String.Format("已执行 {0} 轮操作。请在游戏中核对实际成功次数。", completed)); return; }
                index = 0; passStart = clock.ElapsedMilliseconds;
            }
        }
        catch (Exception e) { Stop(e.Message); }
    }
    IntPtr KeyboardEvent(int code, IntPtr message, IntPtr data)
    {
        if (code >= 0 && capturing)
        {
            var k = (Native.Key)Marshal.PtrToStructure(data, typeof(Native.Key));
            if ((k.Flags & 0x10) == 0 && !(k.Vk >= 119 && k.Vk <= 121))
            {
                bool down = message.ToInt32() == 0x100 || message.ToInt32() == 0x104;
                if (TargetUnchanged()) Add(new RecordedEvent { Kind = down ? 0 : 1, Code = (int)k.Vk });
            }
        }
        return Native.CallNextHookEx(keyboard, code, message, data);
    }
    IntPtr MouseEvent(int code, IntPtr message, IntPtr data)
    {
        if (code >= 0 && capturing && TargetUnchanged())
        {
            var m = (Native.Mouse)Marshal.PtrToStructure(data, typeof(Native.Mouse));
            if ((m.Flags & 1) == 0)
            {
                int msg = message.ToInt32(), kind = -1, button = 0;
                switch (msg)
                {
                    case 0x201: kind=2; button=1; break; case 0x202: kind=3; button=1; break;
                    case 0x204: kind=2; button=2; break; case 0x205: kind=3; button=2; break;
                    case 0x207: kind=2; button=3; break; case 0x208: kind=3; button=3; break;
                    case 0x20b: kind=2; button=(int)(m.Data >> 16)+3; break;
                    case 0x20c: kind=3; button=(int)(m.Data >> 16)+3; break;
                    case 0x20a: kind=4; button=(short)(m.Data >> 16); break;
                    case 0x200: if (clock.ElapsedMilliseconds-lastMove >= 30) { kind=5; lastMove=(int)clock.ElapsedMilliseconds; } break;
                }
                int x=m.Pt.X-origin.X, y=m.Pt.Y-origin.Y;
                if (kind >= 0 && x >= 0 && y >= 0 && x < bounds.R && y < bounds.B)
                    Add(new RecordedEvent { Kind=kind, Code=button, X=x, Y=y });
            }
        }
        return Native.CallNextHookEx(mouse, code, message, data);
    }
    void Add(RecordedEvent e)
    {
        if (recording.Events.Count >= 19500 || clock.ElapsedMilliseconds >= 600000) return;
        if (e.Kind >= 2) { lastX=e.X; lastY=e.Y; }
        if (e.Kind <= 3)
        {
            int k = e.Code + (e.Kind >= 2 ? 256 : 0);
            if (e.Kind % 2 == 0) { if (!held.Add(k)) return; } else if (!held.Remove(k)) return;
        }
        e.T = (int)clock.ElapsedMilliseconds; recording.Events.Add(e);
    }
    void Send(RecordedEvent e)
    {
        var input = new Native.Input();
        if (e.Kind <= 1) { input.Type=1; input.U.Key.Vk=(ushort)e.Code; input.U.Key.Flags=e.Kind==1 ? 2u : 0u;
            if (e.Code == 163 || e.Code == 165 || (e.Code >= 33 && e.Code <= 46) || e.Code == 91 || e.Code == 92 || e.Code == 111)
                input.U.Key.Flags |= 1; }
        else
        {
            int left=Native.GetSystemMetrics(76), top=Native.GetSystemMetrics(77), width=Native.GetSystemMetrics(78), height=Native.GetSystemMetrics(79);
            input.U.Mouse.X=(int)((long)(origin.X+e.X-left)*65535/Math.Max(1,width-1));
            input.U.Mouse.Y=(int)((long)(origin.Y+e.Y-top)*65535/Math.Max(1,height-1));
            input.U.Mouse.Flags=0xC001;
            if (e.Kind==4) { input.U.Mouse.Flags |= 0x800; input.U.Mouse.Data=unchecked((uint)e.Code); }
            if (e.Kind==2 || e.Kind==3)
            {
                uint[] down={0,2,8,32,128,128}; uint[] up={0,4,16,64,256,256};
                input.U.Mouse.Flags |= e.Kind==2 ? down[e.Code] : up[e.Code];
                if (e.Code>=4) input.U.Mouse.Data=(uint)(e.Code-3);
            }
        }
        if (Native.SendInput(1, new[]{input}, Marshal.SizeOf(typeof(Native.Input))) != 1)
            throw new Exception("系统拒绝输入，已停止。请检查游戏与录制程序权限是否一致。");
    }
    void Stop(string message)
    {
        bool closeAfterStop=launchedForPlayback && (playing || armed);
        if (keyboard != IntPtr.Zero) Native.UnhookWindowsHookEx(keyboard);
        if (mouse != IntPtr.Zero) Native.UnhookWindowsHookEx(mouse);
        keyboard=mouse=IntPtr.Zero;
        if (capturing)
        {
            recording.Duration=(int)Math.Min(600000, clock.ElapsedMilliseconds);
            foreach (int k in held)
                recording.Events.Add(new RecordedEvent { T=recording.Duration, Kind=k>=256 ? 3 : 1, Code=k>=256 ? k-256 : k, X=lastX, Y=lastY });
        }
        if (playing)
            foreach (int k in held)
            {
                // Releases never reposition the cursor after a focus change.
                var input=new Native.Input();
                if (k<256) { input.Type=1; input.U.Key.Vk=(ushort)k; input.U.Key.Flags=2;
                    if(k==163 || k==165 || (k>=33 && k<=46) || k==91 || k==92 || k==111) input.U.Key.Flags |= 1; }
                else { uint[] flags={0,4,16,64,256,256}; input.U.Mouse.Flags=flags[k-256]; if(k-256>=4) input.U.Mouse.Data=(uint)(k-259); }
                Native.SendInput(1,new[]{input},Marshal.SizeOf(typeof(Native.Input)));
            }
        held.Clear(); capturing=playing=armed=false; controls.Enabled=true;
        status.Text=message; ShowTimeline();
        if(closeAfterStop && !Disposing && IsHandleCreated) BeginInvoke(new Action(Close));
    }
    void ShowTimeline()
    {
        timeline.BeginUpdate(); timeline.Items.Clear();
        if (recording != null)
        {
            timeline.Items.Add(String.Format("目标：{0} · {1} × {2} · {3:F2} 秒", recording.ProcessName,recording.Width,recording.Height,recording.Duration/1000.0));
            string[] kinds={"按下键", "松开键", "按下鼠标", "松开鼠标", "滚轮", "移动"};
            foreach(var e in recording.Events) timeline.Items.Add(String.Format("{0,8:F2}s  {1} {2}  ({3}, {4})",e.T/1000.0,kinds[e.Kind],e.Code,e.X,e.Y));
        }
        timeline.EndUpdate();
    }
    void Save()
    {
        try
        {
            if(recording==null) throw new Exception("请先录制一轮。");
            if(String.IsNullOrWhiteSpace(name.Text)) throw new Exception("请填写动作名称。");
            recording.Name=name.Text.Trim(); recording.Repeats=(int)repeats.Value; recording.Gap=(int)gap.Value;
            string json=RecordingCodec.ActionJson(recording,Application.ExecutablePath);
            var obj=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(json);
            Directory.CreateDirectory(library);
            string path=Path.Combine(library,obj["id"]+".json");
            string temp=path+".tmp"; File.WriteAllText(temp,json,new UTF8Encoding(false)); File.Move(temp,path);
            status.Text="已保存到 Action Library："+recording.Name+"。关闭录制窗口后在动作库中选择；每次保存为新动作。";
        }
        catch(Exception e) { status.Text=e.Message; }
    }
}
internal static class Program
{
    [STAThread] static int Main(string[] args)
    {
        try
        {
            if(args.Length==1 && args[0]=="--self-test") { SelfTest(); return 0; }
            if(args.Length==3 && args[0]=="--study-five-minutes") {
                try {
                    int pid=Int32.Parse(args[1]);
                    if(Process.GetProcessById(pid).ProcessName!="Maplestory_Classic") throw new Exception("Target must be MapleStory Classic.");
                    Application.Run(new InputStudy(pid,args[2])); return 0;
                } catch(Exception e) { File.WriteAllText(Path.Combine(args[2],"status.txt"),"Failed: "+e.Message); return 1; }
            }
            if(args.Length==2 && args[0]=="--render-test")
            {
                Application.EnableVisualStyles();
                using(var form=new RecorderForm(Path.GetTempPath(),null))
                using(var bitmap=new Bitmap(form.Width,form.Height))
                { form.StartPosition=FormStartPosition.Manual; form.Location=new Point(-30000,-30000);
                  form.Show(); form.PerformLayout(); form.DrawToBitmap(bitmap,new Rectangle(0,0,form.Width,form.Height)); bitmap.Save(args[1]); form.Close(); }
                return 0;
            }
            bool owner;
            using(var mutex=new Mutex(true,"Local\\AntiMicroX.ActionRecorder",out owner))
            {
                if(!owner) return 0;
                Native.SetProcessDPIAware(); Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
                string library=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"AntiMicroX-RandomTurbo","actions");
                Recording loaded=null;
                if(args.Length==2 && args[0]=="--library") library=Path.GetFullPath(args[1]);
                else if(args.Length==2 && args[0]=="--play") loaded=RecordingCodec.Decode(args[1]);
                else if(args.Length!=0) throw new Exception("参数无效。");
                Application.Run(new RecorderForm(library,loaded));
            }
            return 0;
        }
        catch(Exception e) { if(args.Length>0 && args[0]=="--self-test") { Console.Error.WriteLine(e); return 1; }
            if(args.Length==2 && args[0]=="--render-test") { File.WriteAllText(args[1]+".error",e.ToString()); return 1; }
            MessageBox.Show(e.Message,"AntiMicroX 录制"); return 1; }
    }
    static void SelfTest()
    {
        var r=new Recording { ProcessName="test",Width=800,Height=600,Duration=400,Repeats=1000 };
        r.Events.Add(new RecordedEvent { T=0,Kind=0,Code=65 });
        r.Events.Add(new RecordedEvent { T=100,Kind=2,Code=1,X=40,Y=50 });
        r.Events.Add(new RecordedEvent { T=200,Kind=3,Code=1,X=40,Y=50 });
        r.Events.Add(new RecordedEvent { T=300,Kind=1,Code=65 });
        var copy=RecordingCodec.Decode(RecordingCodec.Encode(r));
        string fixture=Environment.GetEnvironmentVariable("ANTIMICROX_RECORDER_TEST_FIXTURE");
        if(!String.IsNullOrEmpty(fixture)) File.WriteAllText(fixture,RecordingCodec.ActionJson(r,Application.ExecutablePath));
        if(copy.Repeats!=1000 || copy.Events.Count!=4 || copy.Events[2].X!=40) throw new Exception("Roundtrip failed");
        var action=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(RecordingCodec.ActionJson(r,"C:\\A & B\\ActionRecorder.exe"));
        var xml=XElement.Parse((string)action["slotsXml"]);
        if(xml.Element("slot").Element("path").Value!="C:\\A & B\\ActionRecorder.exe") throw new Exception("XML escaping failed");
        if(RecordingCodec.Decode(xml.Element("slot").Element("arguments").Value.Substring(7)).Repeats!=1000) throw new Exception("Embedded playback failed");
        r.Events.RemoveAt(3); bool rejected=false;
        try { RecordingCodec.Validate(r); } catch { rejected=true; }
        if(!rejected) throw new Exception("Unbalanced events accepted");
        r.Events.Add(new RecordedEvent { T=300,Kind=1,Code=65 }); r.Repeats=1001; rejected=false;
        try { RecordingCodec.Validate(r); } catch { rejected=true; }
        if(!rejected) throw new Exception("Invalid repeat count accepted");
        Console.WriteLine("PASS: roundtrip, embedded action, escaping, balanced keys, repeat bounds. No desktop input sent.");
    }
}
