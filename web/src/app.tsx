import { Summary } from './summary'
import { ThreadList } from './threadlist'
import { Stacktrace } from './stacktrace'
import { useEffect, useState } from 'react';

function App() {
  const [summary, setSummary] = useState({} as any);
  const [threads, setThreads] = useState([]);
  const [instructionPointMap, setInstructionPointMap] = useState([]);
  const [stackframe, setStackframe] = useState([]);
  const [inclusive, setInclusive] = useState({});
  const [exclusive, setExclusive] = useState({});

  useEffect(() => {
    let id = setInterval(() => {
      uwu.post({type: "snapshot"})
    }, 120);

    const callback = msg => {
      if (msg.data.type === 'snapshot') {
        const json = msg.data.data;
        setSummary({
          process_id: json.process_id,
          process_name: json.process_name,
          process_cpu_usage: json.process_cpu_usage,
          process_phys_mem_usage: json.process_phys_mem_usage,
          process_virt_mem_usage: json.process_virt_mem_usage,
          thread_id: json.thread_id,
          elapsed: json.elapsed,
          samples: json.samples,
          state: json.state,
        })
        setThreads(json.threads);
        setInstructionPointMap(json.instruction_point_map);
        setStackframe(json.stack_frame);
        setInclusive(json.inclusive);
        setExclusive(json.exclusive);
      }
    };
    uwu.watch(callback);
    uwu.post({ type: "process", rule: "livetrace.exe" })
    uwu.post({ type: "snapshot" });

    return () => { 
      clearInterval(id);
      uwu.unwatch(callback);
    };
  }, []);  // run once

  return (
    <div id="container">
      <div id="top">
        <Summary summary={summary} />
      </div>
      <div id="bottom">
        <ThreadList threads={threads} threadId={summary.thread_id} instructionPointMap={instructionPointMap} />
        <Stacktrace stackframe={stackframe} inclusive={inclusive} exclusive={exclusive} instructionPointMap={instructionPointMap} />
      </div>
    </div>
  );
}

export default App;