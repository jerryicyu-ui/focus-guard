# 產生第三週成果報告 docs/final_report.html：正文寫在 docs/final_report_body.html，
# 附錄的原始測試紀錄直接從 docs/test-data/week3/*.csv 產生，避免手抄出錯。
# 之後用 scripts/build_proposal.ps1 -Source final_report.html -Output Final_Report.pdf 轉成 PDF。
import csv
import glob
import html
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'docs', 'test-data', 'week3')

RUNS = [
    ('初測', '原始版本（第二週程式：確認 3 秒、手機 ≥ 50 才算看到）'),
    ('重測', '第一輪改進（只加遲滯門檻）'),
    ('重測2', '最終版本（遲滯門檻＋判定時須看到手機＋確認 2 秒）'),
]
CONDITIONS = ['normal', 'occlusion', 'nontarget']


def read_csv(path):
    with open(path, encoding='utf-8-sig') as f:
        return list(csv.DictReader(f))


def run_file(phase, condition, suffix='.csv'):
    files = sorted(f for f in glob.glob(os.path.join(DATA, f'{phase}_{condition}_*{suffix}'))
                   if suffix != '.csv' or not f.endswith('.void.csv'))
    return files[-1] if files else None


def records_table(phase, title):
    out = [f'<h3>{html.escape(phase)}：{html.escape(title)}</h3>']
    for condition in CONDITIONS:
        path = run_file(phase, condition)
        if not path:
            continue
        rows = read_csv(path)
        ok = sum(r['是否符合預期'] == '是' for r in rows)
        out.append(f'<p class="rec-head"><b>{html.escape(rows[0]["條件"])}</b>　{ok}/{len(rows)} 符合預期　'
                   f'<span class="sub">預期行為：{html.escape(rows[0]["預期行為"])}</span></p>')
        out.append('<table class="rec"><tr><th style="width:4%">#</th><th>實際行為</th><th style="width:6%">符合</th>'
                   '<th style="width:7%">反應秒數</th><th style="width:7%">手機最高分</th><th style="width:30%">備註</th></tr>')
        for r in rows:
            cls = '' if r['是否符合預期'] == '是' else ' class="fail"'
            out.append(f'<tr{cls}><td>{r["測試編號"]}</td><td>{html.escape(r["實際行為"])}</td><td>{r["是否符合預期"]}</td>'
                       f'<td>{r["反應秒數"]}</td><td>{r["手機最高分"]}</td><td>{html.escape(r["備註"])}</td></tr>')
        out.append('</table>')
    return '\n'.join(out)


def void_table():
    out = ['<table class="rec"><tr><th style="width:10%">階段</th><th style="width:12%">條件</th><th style="width:5%">#</th>'
           '<th>實際行為</th><th style="width:16%">作廢原因（測試者填寫）</th></tr>']
    names = {'normal': '正常', 'occlusion': '遮擋', 'nontarget': '非目標'}
    for phase, _ in RUNS:
        for condition in CONDITIONS:
            path = run_file(phase, condition, '.void.csv')
            if not path:
                continue
            for r in read_csv(path):
                out.append(f'<tr><td>{phase}</td><td>{names[condition]}</td><td>{r["測試編號"]}</td>'
                           f'<td>{html.escape(r["實際行為"])}</td><td>{html.escape(r["作廢原因"])}</td></tr>')
    out.append('</table>')
    return '\n'.join(out)


def main():
    with open(os.path.join(ROOT, 'docs', 'final_report_body.html'), encoding='utf-8') as f:
        body = f.read()
    appendix = [records_table(phase, title) for phase, title in RUNS]
    body = body.replace('<!--RECORDS-->', '\n'.join(appendix)).replace('<!--VOIDS-->', void_table())
    with open(os.path.join(ROOT, 'docs', 'final_report.html'), 'w', encoding='utf-8') as f:
        f.write(body)
    print('docs/final_report.html written')


if __name__ == '__main__':
    main()
