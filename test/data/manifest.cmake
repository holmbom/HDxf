# External DXF test files. They are not checked in: test/CMakeLists.txt downloads them to
# ${CMAKE_BINARY_DIR}/test-data/ when they are missing. Source, license and facit for each
# file: test/data/README.md.
#
# hdxf_test_file(<local path> <URL pinned to a commit> <SHA256>)

set(ACADSHARP "https://raw.githubusercontent.com/DomCR/ACadSharp/29137f3adaf75ddebd532179eda1e811525ca574/samples")
set(EZDXF "https://raw.githubusercontent.com/mozman/ezdxf/7b202613e0d689955b8932a22bcbcf68bdd684f0")
set(JSCAD "https://raw.githubusercontent.com/jscad/sample-files/f6f6485e7286a50c4c304d697be9cc9f7e64beb6")
set(ASPOSE "https://raw.githubusercontent.com/aspose-cad/Aspose.CAD-for-.NET/44108cca2375adfd95f1a6f5a9632a952d44f1b2")
set(OPENAEC "https://raw.githubusercontent.com/OpenAEC-Foundation/ifccad-prototype/6f970792a782eaf2ef40c556c3bc542f9f70b0b9/DXF%20DWG%20samples/libre")
set(GNGEOVANE "https://raw.githubusercontent.com/gnGeovane/CNC-Plotter/feacce35b07115058b3e004d7dc19e7c34fb8a4f")
set(OXCART "https://raw.githubusercontent.com/peterfelfer/Oxcart/8b985484d738cbd412e027a4a9271681ee155cae/CAD/cryo/cryo%20system%20MC")
set(LIBREDWG "https://raw.githubusercontent.com/LibreDWG/libredwg/a8ccb1f314d9feb2215be67d1f6f2a3d64b2c9a0/test/test-data")

# ACadSharp — one drawing saved by AutoCAD as R12 … 2018, ASCII and binary
hdxf_test_file(acadsharp/sample_AC1009_ascii.dxf  ${ACADSHARP}/sample_AC1009_ascii.dxf  d0039feca2eda1ead9a7135854c62e0818d0ae0af842a44e1645cf8c8523fd75)
hdxf_test_file(acadsharp/sample_AC1009_binary.dxf ${ACADSHARP}/sample_AC1009_binary.dxf dc8612a2670d8bbb0697d6a4e51b197b6bf2055c08c95b8c646515bb63ca947f)
hdxf_test_file(acadsharp/sample_AC1015_ascii.dxf  ${ACADSHARP}/sample_AC1015_ascii.dxf  4f7d7f0d3957d4d32c6c43e4c600eea8e90fc0fcb1b36b11256095a9789a923f)
hdxf_test_file(acadsharp/sample_AC1015_binary.dxf ${ACADSHARP}/sample_AC1015_binary.dxf a3b3d56311371355b0fc01c50c2fa8ac741ed87429114fe9fcdfb3d2ad15d535)
hdxf_test_file(acadsharp/sample_AC1018_ascii.dxf  ${ACADSHARP}/sample_AC1018_ascii.dxf  71117bafa8f0d9816607768c8fb21676c83f99892c537711a68c24aa7e06e798)
hdxf_test_file(acadsharp/sample_AC1018_binary.dxf ${ACADSHARP}/sample_AC1018_binary.dxf d659b8e7964eb30700660eea34454696bbbfc927726c3417d93df8776e88e225)
hdxf_test_file(acadsharp/sample_AC1021_ascii.dxf  ${ACADSHARP}/sample_AC1021_ascii.dxf  39ebdd9a48fef7a6240a04e3b5ca6d45ba5bb30b560fb9dadf26774e1af83731)
hdxf_test_file(acadsharp/sample_AC1021_binary.dxf ${ACADSHARP}/sample_AC1021_binary.dxf 9650c4f2b44f064e09c8ba10ccf558f9438bf7779bed18e6144887930ddb7cb1)
hdxf_test_file(acadsharp/sample_AC1024_ascii.dxf  ${ACADSHARP}/sample_AC1024_ascii.dxf  c97e857047ad4cecd84754ea1a8638e47508e339fd489f1e895ee7332127b372)
hdxf_test_file(acadsharp/sample_AC1024_binary.dxf ${ACADSHARP}/sample_AC1024_binary.dxf 6a99aed5d669235a12f592d5cce426c6fba29b5462bae8d76f98187a2fdda22f)
hdxf_test_file(acadsharp/sample_AC1027_ascii.dxf  ${ACADSHARP}/sample_AC1027_ascii.dxf  4f26752008643642c37f7bab410b23cfd8b6dd9ee3aaa19746b612ea956b61e6)
hdxf_test_file(acadsharp/sample_AC1027_binary.dxf ${ACADSHARP}/sample_AC1027_binary.dxf 9a8c4dc15828f62b587273f41cfac2a63ce7614841844a15a15248b49cab7e14)
hdxf_test_file(acadsharp/sample_AC1032_ascii.dxf  ${ACADSHARP}/sample_AC1032_ascii.dxf  c2934f616ecd7097e773b85e5d6e0548d21ccc635abcc8e7276f7d4a585a7ae2)
hdxf_test_file(acadsharp/sample_AC1032_binary.dxf ${ACADSHARP}/sample_AC1032_binary.dxf e2c59abecc7bc0fd0ace7b9b6f083d2a16d36077b7a7d7ceccd2aa7edfb348e1)

# ezdxf — small files: R13/R14, binary, Unicode, R12 edge cases, other programs
hdxf_test_file(ezdxf/small_r13.dxf                 ${EZDXF}/integration_tests/data/small_r13.dxf                 a1007452f8ada2e71fdef60f79923924045cb371f37eeab896a61f5628573d0b)
hdxf_test_file(ezdxf/small_r14.dxf                 ${EZDXF}/integration_tests/data/small_r14.dxf                 92e792f8f9169e4226c9b7dbe848ba9afb96d3e388e6297c55481e1941c65fb0)
hdxf_test_file(ezdxf/bin_dxf_r12.dxf               ${EZDXF}/integration_tests/data/bin_dxf_r12.dxf               1e7a904d67bc7036bd3f78a6273cdf28124ba7e8b731604c0593288182f2aa0e)
hdxf_test_file(ezdxf/bin_dxf_r13.dxf               ${EZDXF}/integration_tests/data/bin_dxf_r13.dxf               c65782683815ff0f1680d26207da1637a486f96b30bc215685e26cf7adee8464)
hdxf_test_file(ezdxf/bin_dxf_r14.dxf               ${EZDXF}/integration_tests/data/bin_dxf_r14.dxf               177a97e5dc86ca106d0dd6365d139122fc54ff78df87670847eb7174a8bbd5eb)
hdxf_test_file(ezdxf/bin_dxf_r2000.dxf             ${EZDXF}/integration_tests/data/bin_dxf_r2000.dxf             bdbe44e042bc6f56fbf5bd4fde0053fa1b82dcf90305f21a521a7b70d2c37320)
hdxf_test_file(ezdxf/dxf_unicode.dxf               ${EZDXF}/integration_tests/data/dxf_unicode.dxf               41c2b8a11abea166ff3e4aa14a415c3bbbec42efa008992e3addc77c4f57d33a)
hdxf_test_file(ezdxf/Minimal_DXF_AC1009.dxf        ${EZDXF}/examples_dxf/Minimal_DXF_AC1009.dxf                  7286a35c85164d0165b6e0e2bd8093c25f818cdc31b9abd34a46646924fcccc3)
hdxf_test_file(ezdxf/R12_with_trash_beyond_EOF.dxf ${EZDXF}/integration_tests/data/R12_with_trash_beyond_EOF.dxf 15f5dbecb643a46742e9e6662dcabb2b126f0bdd77672424db96329e13b317f5)
hdxf_test_file(ezdxf/r12_blocks_with_no_names.dxf  ${EZDXF}/integration_tests/data/r12_blocks_with_no_names.dxf  5516994924c40e73822b2844ac01e73f8f7603673a1688f22f3a293b45dd10d0)
hdxf_test_file(ezdxf/Leica_Disto_S910.dxf          ${EZDXF}/integration_tests/data/Leica_Disto_S910.dxf          5ed4ceca489c9541b075805a38b2990bc15946b27a8d14b5f43ff90d5ef82507)
hdxf_test_file(ezdxf/cc_dxflib.dxf                 ${EZDXF}/integration_tests/data/cc_dxflib.dxf                 060ba0c25d29c7b84c493bb61a7664c58835741a8b5e738cd658923f35ad0779)
hdxf_test_file(ezdxf/Minimal_DXF_AC1021.dxf        ${EZDXF}/examples_dxf/Minimal_DXF_AC1021.dxf                  6866a50766e178f19356607ee3d3fe619f9f46cfcd2d7d5969f1077eb675464d)

# ACIS (3DSOLID, REGION) — drawings of solids with AutoCAD's own $EXTMIN/$EXTMAX (rättning R.9)
hdxf_test_file(jscad/3Ddraw.dxf                    ${JSCAD}/dxf/autocad2017/3Ddraw.dxf                       26b36ce678dc34da682c0c89654bed4408918cd9754734b2fdfed45fb6304a20)
hdxf_test_file(aspose/conic_pyramid.dxf            ${ASPOSE}/Examples/Data/DXF-Drawings/conic_pyramid.dxf    2b9c26e8452deca02f8d6d3e80038dfd515edfebb04fcb2163d098c1a285a60e)
hdxf_test_file(ezdxf/3dsolids.dxf                  ${EZDXF}/examples/acistools/3dsolids.dxf                  8193d9917354a9165d500098aa31209218f0b3d6bf3b24632b89003295806c4d)

# AutoCAD 2017's new drawing saved as 2007 — the standard objects of a new 2007 drawing (phase 3, B34)
hdxf_test_file(jscad/empty2007.dxf                 ${JSCAD}/dxf/autocad2017/empty2007.dxf                    80ebb3ffec87dbf00a40047bd4af885cf7baf63c12d55df88066fafea6ed3c19)

# MULTILEADER (phase 3, B36): LibreDWG's Leader drawing saved by AutoCAD as 2000, 2004 and 2007
# (via OpenAEC's ifccad-prototype), and five multileaders in a 2000 file of another program
# (Solid Edge's fonts; the AcCmColor "none")
hdxf_test_file(libre/Leader_2000.dxf               ${OPENAEC}/2000/Leader.dxf                                d15b972e2c6132a7b28681c522f2573110781faa38c02d0e2693b71701317c72)
hdxf_test_file(libre/Leader_2004.dxf               ${OPENAEC}/2004/Leader.dxf                                0530bf9d0ab416076e1cadff88aa3f4f0c78a037ff7369fc542772d4f23cfef3)
hdxf_test_file(libre/Leader_2007.dxf               ${OPENAEC}/2007/Leader.dxf                                196fb93a8f1f760288147db49dafbf31fafafd509c986ec66e24f23914536233)
hdxf_test_file(gngeovane/Universal_Pen_Holder.dxf  "${GNGEOVANE}/Modelos%203D/Universal-Pen-Holder%20(for%20a%20CNC)%20-%205806108/files/Universal_Pen_Holder.dxf" 86ee84901ca1250a66cb333e4029911bb770e9d365662a40488dcee45bf389d2)

# MULTILEADER proxy graphics (phase 3, B38): drawings of Autodesk's (2013 files of the Oxcart
# project, GPL-3.0) with balloons — a block with attributes, a block of MTEXT and arcs — and
# text multileaders with underlines, trailing spaces and an empty text
hdxf_test_file(oxcart/cryo_clamp_upper.dxf         "${OXCART}/cryo%20clamp%20upper%20Drawing%20v2%20Sheet1.dxf"          c3b3b6abb201582ad884779a8cc2bff297eb6ddd7124403ab3dff566efb8e7e1)
hdxf_test_file(oxcart/CY7_diode_clamp.dxf          "${OXCART}/CY7%20diode%20clamp%20Drawing%20v2%20Sheet1.dxf"           6084b9eb621aab231792eaeb278517385a39228ccba50590c69accab1eb49cf6)
hdxf_test_file(oxcart/cryo_interface_v3.dxf        "${OXCART}/cryo%20interface%20v3%20Drawing%20v20%20Sheet6.dxf"        df030ebd6ad15ecbe3bd1ddfef2c740a1e0014c52c165a3989a72e04ff20cb77)
hdxf_test_file(oxcart/cryo_interface_v2_weld.dxf   "${OXCART}/cryo%20interface%20v2%20weld%20Drawing%20v3%20Sheet1.dxf"  d592bd152348e4e735732cde6b0e00821bb5e13d3542bf6d039126ad68db4e29)

# Surfaces and HELIX (phase 3, B39): LibreDWG's Surface drawing saved by AutoCAD as 2004 (all five
# surface types, with their isolines as proxy graphics) and its Helix drawing as 2007 (via OpenAEC),
# and AutoCAD 2017's 3Dlines (2013: four extruded surfaces, ACIS in ACDSDATA)
hdxf_test_file(libre/Surface_2004.dxf              ${OPENAEC}/2004/Surface.dxf                               a6cbf34daaf6c2c82173fa90f0979ed8a0a8653155f392d5e66e4746f3dd8daa)
hdxf_test_file(libre/Helix_2007.dxf                ${OPENAEC}/2007/Helix.dxf                                 23c6d311638028ae62ce22347899112873439c85326b579fcae76a5a47da9f3c)
hdxf_test_file(jscad/3Dlines.dxf                   ${JSCAD}/dxf/autocad2017/3Dlines.dxf                      4bc4204815f85f912a2d29c85b07d5fcdc2cd3bcb65763e3bf854309e295d562)

# DXF 2010 (phase 4): AutoCAD 2017's new drawing saved as 2010 — the standard objects of a new 2010
# drawing (B45) — and LibreDWG's Leader and gh209_1 drawings saved by AutoCAD as 2010 (via OpenAEC;
# gh209_1 has a GEODATA in the 2010 form)
hdxf_test_file(jscad/empty2010.dxf                 ${JSCAD}/dxf/autocad2017/empty2010.dxf                    ac1fff8950d422494a5fea6d001d3c0e9b9bee555797282ae757600e553a7ad7)
hdxf_test_file(libre/Leader_2010.dxf               ${OPENAEC}/2010/Leader.dxf                                ec3d74eb6452c98f4d4aeab404be4a3aa1b2502709fe09b85b74d2076ccf1e00)
hdxf_test_file(libre/gh209_1_2010.dxf              ${OPENAEC}/2010/gh209_1.dxf                               4cf537530878e63bcde30e90a32646488428ef794a83487e548ccbb53bd6c411)

# MESH (phase 4, B48/B49): AutoCAD 2017's two box meshes (AC1027), one with ten creases "always" (-1)
hdxf_test_file(jscad/3Dmesh01.dxf                  ${JSCAD}/dxf/autocad2017/3Dmesh01.dxf                     280e9d4c1111f9e9283416fc9011c832adda97f60c51b3be6c2a83ef4f2b3684)

# GEODATA (phase 4, B50): ACadSharp's geolocated drawing saved by AutoCAD as 2007 (version 1) and
# 2018 (version 3, its coordinate system in 303 parts)
hdxf_test_file(acadsharp/geoloc_AC1021.dxf         ${ACADSHARP}/geolocation/geoloc_AC1021.dxf                bbfc534edcfcb7bb9a39d5bb8aa5808fe8fad97425708623f7e6b0bb2261d2c1)
hdxf_test_file(acadsharp/geoloc.dxf                ${ACADSHARP}/geolocation/geoloc.dxf                       30d1985e3838494ed8968287933294ffac4896cd0b43e77b2ffea946551e539e)

# DXF 2013 (phase 5): AutoCAD 2017's new drawing saved as 2013 — the standard objects of a new 2013
# drawing (B59) — and LibreDWG's example drawing saved by AutoCAD as 2007, 2010 and 2013 (three ACIS
# bodies, a MULTILEADER with 295 1, its style with 298 1)
hdxf_test_file(jscad/empty2013.dxf                 ${JSCAD}/dxf/autocad2017/empty2013.dxf                    ae3c3162febe13fdee3b463506cf257bade715181aeaf54403321ceec680e92e)
hdxf_test_file(libre/example_2007.dxf              ${LIBREDWG}/example_2007.dxf                              c51b813e30c5686090ee3fefb6a04cd5f75bcad4eec994ad6e17b3b19d129d43)
hdxf_test_file(libre/example_2010.dxf              ${LIBREDWG}/example_2010.dxf                              fcd9ce4b2cf58c5b1bed2b6353a71c6350640d4038e31875c97a763a63744124)
hdxf_test_file(libre/example_2013.dxf              ${LIBREDWG}/example_2013.dxf                              951f7910d1ea8ccc2d1eae9556c2f3f05a0197e6379fbf13d40f660df95a9ce3)

# HELIX in 2013 (phase 5, B65): AutoCAD 2017's helices saved as 2013 — three in 3D, three flat
hdxf_test_file(jscad/3Dhelix.dxf                   ${JSCAD}/dxf/autocad2017/3Dhelix.dxf                      84731ebc5bbc3b158427b67cc12e02ebbdc38adf389b1d99f1c2249285357cd0)
hdxf_test_file(jscad/2Dhelix.dxf                   ${JSCAD}/dxf/autocad2017/2Dhelix.dxf                      7e7a1d538c19937a205818a79767014d01f62721908e871c68927a25d29b792c)

# DXF 2018 (phase 6): LibreDWG's example drawing saved by AutoCAD as 2018 (the pair of example_2013:
# the same 253 header variables; the bodies' SAB 22300 — step 6.8). The file in LibreDWG's
# repository has CRLF line ends; without them its SHA256 is
# 3938567f10827d17ffab72bbc15d2d11dd89a2e1b006d959537495f616d02c11 (OpenAEC's copy)
hdxf_test_file(libre/example_2018.dxf              ${LIBREDWG}/example_2018.dxf                              e64b0cfb6e1587f9d2bb4387d59064efa5bc667cda9ecbc00313459b304bb997)

# MTEXT's columns and frame before 2018 (phase 6, B73/B74): ezdxf's integration test data saved by AutoCAD
# 2013 (AC1027, $ACADMAINTVER 105) — static and dynamic columns as entities of their own, linked by
# ACAD_MTEXT_COLUMNS, and the text frame as XDATA and LWPOLYLINEs (MIT licence)
hdxf_test_file(ezdxf/mtext_framed_columns.dxf      ${EZDXF}/integration_tests/data/mtext_framed_columns.dxf      bdcc561aebf79edf51a75e2d545760c4274d56737dad3561ba14187156f958c3)
hdxf_test_file(ezdxf/mtext_text_frame.dxf          ${EZDXF}/integration_tests/data/mtext_text_frame.dxf          380ca558f6960bc58d235d239e8131cef6f0bc4127f97b34296417d89a270dec)

# R13 / R14 (phase 7, step 7.1): ASCII files saved by AutoCAD of that time (or by an AutoCAD of 2000 .. 2004 as
# R13/R14), found on GitHub. Producer, version, maintenance and what each shows: test/data/README.md.
# No AutoCAD that exists can save R13/R14 DXF (SAVEAS gives R12, 2000 .. 2018 only), so these are all there is.
# LibreDWG's r13/v.dxf is GPL-3.0 test data (only read), gvSIG's dxfprueba.dxf GPL-3.0 (only read),
# SpatialAuxetic MIT, jscad/sample-files as above.
set(GVSIG "https://raw.githubusercontent.com/iCarto/gvsig-siga/278da442d4cf01bde67b83b6ec0713e1639ccae7")
set(SPATIALAUXETIC "https://raw.githubusercontent.com/ChiangYuChou/SpatialAuxetic/c20201ebd1dab5d25f2c41dfc060b398591c6417")
hdxf_test_file(libre/r13_v.dxf                     ${LIBREDWG}/r13/v.dxf                                     86531656820742506d5964333c3a82d4554e649174ee67877b3f876f252aa0a1)
hdxf_test_file(gvsig/dxfprueba.dxf                 ${GVSIG}/libGDBMS/src/test/resources/dxfprueba.dxf        08ba5260458a8e825bffd296869245be25b104c66d2fcc28e5b67e9566f165a6)
hdxf_test_file(spatialauxetic/HEAD.DXF             ${SPATIALAUXETIC}/MATLAB/HEAD.DXF                         a405d99bf176e6f7c49c8f9866413ad2dd757fb6edda63d6e241a055c9aaa25f)
hdxf_test_file(spatialauxetic/simp.dxf             ${SPATIALAUXETIC}/MATLAB/simp.dxf                         1ff13a89a47092b3d771c34649b0ca45b2ff822d03e624d8815afe2278b64eb4)
hdxf_test_file(jscad/pyramid-base.dxf              ${JSCAD}/dxf/others/pyramid-base.dxf                      0e3709cb42438aa23b8534304b46a382622453b5ad2756f6484e4bc392e3e73a)
