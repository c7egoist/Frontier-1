Readme.md  

## Editor previews

Only `Experimental/ProjectZeroEditor` and `Experimental/FrontierEditor` were
imported from [SultanAladin/Frontier at `63db120`](https://github.com/SultanAladin/Frontier/tree/63db1201d0cea199c7276c2126e62812b04821b9).

From the repository root, run these in separate terminals:

```sh
python3 -m http.server 8092 --bind 0.0.0.0 --directory Experimental/ProjectZeroEditor
```

```sh
cd Experimental/FrontierEditor
npm ci
npm run dev -- --port 5173 --strictPort
```

Open `/` on port 8092 for Project Zero Editor and on port 5173 for Frontier
Editor. This checkout does not include the native Construct backend or the
separate Fracture Editor, so features that launch or call those are not available.

To rebuild Project Zero after editing its sources, run
`node Experimental/ProjectZeroEditor/Build.mjs` from the root after `npm ci`.
The build reuses the assets embedded in its checked-in standalone `index.html`.
