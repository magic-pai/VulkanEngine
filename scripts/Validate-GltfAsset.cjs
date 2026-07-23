const fs = require("fs");
const path = require("path");

if (process.argv.length < 5) {
    console.error("Usage: node Validate-GltfAsset.cjs <validator-package> <asset> <report>");
    process.exit(2);
}

const validatorPackage = path.resolve(process.argv[2]);
const assetPath = path.resolve(process.argv[3]);
const reportPath = path.resolve(process.argv[4]);
const validator = require(validatorPackage);

validator.validateBytes(new Uint8Array(fs.readFileSync(assetPath)), {
    uri: assetPath,
    maxIssues: 10000,
}).then((report) => {
    fs.mkdirSync(path.dirname(reportPath), { recursive: true });
    fs.writeFileSync(reportPath, JSON.stringify(report, null, 2));
    console.log(JSON.stringify({
        validator: report.validatorVersion,
        errors: report.issues.numErrors,
        warnings: report.issues.numWarnings,
        infos: report.issues.numInfos,
        hints: report.issues.numHints,
        drawCalls: report.info.drawCallCount,
        materials: report.info.materialCount,
        totalVertices: report.info.totalVertexCount,
        totalTriangles: report.info.totalTriangleCount,
    }));
    process.exit(report.issues.numErrors > 0 ? 1 : 0);
}).catch((error) => {
    console.error(error);
    process.exit(2);
});
