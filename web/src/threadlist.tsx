import { Fragment } from "react";

export function ThreadList(props) {
  const resolve = offset => {
    const ip = props.instructionPointMap[offset];
    if (!ip) return "(unknown)";
    const displacement = parseInt(ip.displacement, 16);
    return `${ip.function_name}+0x${displacement}`
  };

  const select = id => {
    uwu.post({type: "thread", thread: id})
  };

  return (
    <div id="threadlist">
      <div className="table">
        <p className="th">ID</p>
        <p className="th">ADDRESS</p>
        <p className="th">CYCLES</p>
        {
          props.threads.map((_, i) => (
            <Fragment key={i}>
              <p onMouseDown={() => select(_.id)} className={props.threadId === _.id ? "active" : ""}>{_.id}</p>
              <p onMouseDown={() => select(_.id)}>{resolve(_.instruction_offset ?? -1)}</p>
              <p onMouseDown={() => select(_.id)}>{new Intl.NumberFormat('en-US').format(_.cycles)}</p>
            </Fragment>
          ))
        }
      </div>
    </div>
  )
}