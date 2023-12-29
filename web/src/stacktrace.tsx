import { Fragment } from "react";

export function Stacktrace(props) {
  const resolve_function = offset => {
    const ip = props.instructionPointMap[offset];
    if (!ip) return "(unknown)";
    const displacement = parseInt(ip.displacement, 16);
    return `${ip.function_name}+0x${displacement}`
  };

  const resolve_source = offset => {
    const ip = props.instructionPointMap[offset];
    if (!ip) return "(unknown)";
    return ip.source_name ? `${ip.source_name}:${ip.source_line}` : ""
  };

  const inclusive = offset => props.inclusive[offset.toString()] || 0;
  const exclusive = offset => props.exclusive[offset.toString()] || 0;

  const inclusive_ranking = () =>
    Object.keys(props.inclusive || []).map(_ => (
      {
        address: resolve_function(_),
        count: props.inclusive[_]
      }
    ))
    .sort((a, b) => b.count - a.count)
    .slice(0, 20)
    .map((_, _i, a) => ({
      ..._,
      percentage: (_.count / a[0].count * 100)
    }))

  const exclusive_ranking = () => 
    Object.keys(props.exclusive || []).map(_ => (
      {
        address: resolve_function(_),
        count: props.exclusive[_]
      }
    ))
    .sort((a, b) => b.count - a.count)
    .slice(0, 20)
    .map((_, _i, a) => ({
      ..._,
      percentage: (_.count / a[0].count * 100)
    }));

  return (
    <div id="stacktrace">
      <div className="table">
        <p className="th"></p>  
        <p className="th">ADDRESS</p>
        <p className="th">SOURCE</p>
        <p className="th">INCLUSIVE</p>
        <p className="th">EXCLUSIVE</p>
        {props.stackframe?.map((_, i) =>
          <Fragment key={i}>
            <p>{i}</p>
            <p>{resolve_function(_.instruction_offset)}</p>
            <p>{resolve_source(_.instruction_offset)}</p>
            <p>{inclusive(_.instruction_offset)}</p>
            <p>{exclusive(_.instruction_offset)}</p>
          </Fragment>
        )}
      </div>
      <div className="ranking">
        <div className="inclusive">
          <p className="desc">INCLUSIVE TOP 20</p>
          {
            inclusive_ranking().map((_, i) => (
              // @ts-ignore
              <p key={i} style={{'--percentage': _.percentage + '%'}}>{_.address} ({_.count})</p>
            ))
          }
        </div>
        <div className="exclusive">
          <p className="desc">EXCLUSIVE TOP 20</p>
          {
            exclusive_ranking().map((_, i) => (
              // @ts-ignore
              <p key={i} style={{'--percentage': _.percentage + '%'}}>{_.address} ({_.count})</p>
            ))
          }
          </div>
        </div>
      </div>
  );
}
/*
              <div className="bar" data-percentage={_.percentage} key={i}>
              </div>
              */