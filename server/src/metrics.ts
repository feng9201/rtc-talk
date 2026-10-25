/**
 * 最小 Prometheus 文本格式导出（不引第三方客户端）。
 * 只做 pull 型指标：请求 /metrics 时按当前状态现算，无需全局计数器。
 */
export interface Sample {
  name: string;
  help: string;
  type: 'counter' | 'gauge';
  labels?: Record<string, string | number>;
  value: number;
}

function labelText(labels?: Record<string, string | number>): string {
  if (!labels) return '';
  const parts = Object.entries(labels).map(
    ([k, v]) => `${k}="${String(v).replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`,
  );
  return parts.length > 0 ? `{${parts.join(',')}}` : '';
}

/** 同名指标的 HELP/TYPE 只输出一次，非有限数按 0 处理 */
export function renderMetrics(samples: Sample[]): string {
  const groups = new Map<string, Sample[]>();
  for (const s of samples) {
    const list = groups.get(s.name);
    if (list) list.push(s);
    else groups.set(s.name, [s]);
  }
  const lines: string[] = [];
  for (const [name, list] of groups) {
    const first = list[0];
    if (!first) continue;
    lines.push(`# HELP ${name} ${first.help}`, `# TYPE ${name} ${first.type}`);
    for (const s of list) {
      lines.push(`${name}${labelText(s.labels)} ${Number.isFinite(s.value) ? s.value : 0}`);
    }
  }
  return `${lines.join('\n')}\n`;
}
