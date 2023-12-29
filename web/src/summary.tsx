import { Clock3, Cpu, Layers, MemoryStick, PackagePlus, PackageSearch, Pause, RotateCcw } from 'lucide-react'
import { useEffect, useState } from 'react';

export function Summary(props) {
  const [target, setTarget] = useState("livetrace.exe");

  useEffect(() => {
    uwu.post({ type: "process", rule: target });
  }, [target])

  const targetProcess = () => {
    if (props.summary.process_name && props.summary.process_id) {
      return `${props.summary.process_name} (${props.summary.process_id})`;
    } else {
      return "(unknown)";
    }
  }

  const restart = () => {
    uwu.post({ type: "process", rule: target });
  };

  const pause = () => {
    uwu.post({ type: "pause" });
  };

  return (
    <div className="summary">
      <div className="header process-start">
        <PackagePlus strokeWidth={0.5} />
        <p className="desc">TARGET PROCESS (NAME OR PID)</p>
        <p className="stack">
          <input type="text" value={target} onChange={e => setTarget(e.target.value)}></input>
          <RotateCcw onMouseDown={() => restart()} className="restart" size={30} strokeWidth={1.0} />
          <Pause onMouseDown={() => pause()} className="pause" size={30} strokeWidth={1.0} />
        </p>
      </div>
      <div className="header process-summary">
        <PackageSearch strokeWidth={0.5} />
        <p className="desc">CURRENT PROCESS</p>
        <p className="value">{targetProcess()}</p>
      </div>
      <div className="header time">
        <Clock3 strokeWidth={0.5} />
        <p className="desc">STATE</p>
        <p className="value">{
          props.summary.state === 0 ? "Idle" :
          props.summary.state === 1 ? "Running" :
          props.summary.state === 2 ? "Exited" :
          props.summary.state === 3 ? "Failed" :
          props.summary.state === 4 ? "Paused" :
          "Unknown"
        }</p>
      </div>
      <div className="header stacktrace">
        <Layers size={48} strokeWidth={0.5} />
        <p className="desc">STACKTRACES</p>
        <p className="value">{new Intl.NumberFormat('en-US').format(props.summary.samples)} ({(props.summary.samples / (props.summary.elapsed / 1000)).toFixed(2)}/sec)</p>
      </div>
      <div className="header cpu">
        <Cpu size={48} strokeWidth={0.5} />
        <p className="desc">CPU USAGE</p>
        <p className="value">{(props.summary.process_cpu_usage * 100).toFixed(2)}%</p>
      </div>
      <div className="header mem">
        <MemoryStick size={48} strokeWidth={0.5} />
        <p className="desc">PHYS MEM USAGE</p>
        <p className="value">{new Intl.NumberFormat('en-US').format(props.summary.process_phys_mem_usage)}</p>
      </div>
      <div className="header mem">
        <MemoryStick size={48} strokeWidth={0.5} />
        <p className="desc">VIRT MEM USAGE</p>
        <p className="value">{new Intl.NumberFormat('en-US').format(props.summary.process_virt_mem_usage)}</p>
      </div>
    </div>
  )
}


