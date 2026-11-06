// Read-only adapter to the existing CI policy. Never calls its run/main functions.
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';

const request = JSON.parse(readFileSync(0, 'utf8'));
const ci = await import(pathToFileURL(resolve(request.repo, 'scripts/ci/suite.mjs')));
let result;
if (request.action === 'contract') {
  result = {testBaselines: ci.testBaselines, plans: Object.fromEntries(
    ['windows', 'linux'].map(p => [p, ci.plan(p, 'full')]))};
} else if (request.action === 'assess') {
  const tests = [];
  for (const item of request.logs) {
    try {
      tests.push({id: item.id, results: ci.testEvidence(item.gate, item.text, item.count)});
    } catch (error) {
      tests.push({id: item.id, error: error.message});
    }
  }
  result = {verdict: ci.combinedVerdict(request.mode, request.reports, request.exits), tests};
} else if (request.action === 'inspect') {
  result = ci.snapshot(request.target);
} else {
  throw Error('Unknown read-only action');
}
process.stdout.write(JSON.stringify(result));
