#include "semantic_fs/ocr/ocr_attempt_runner.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <sstream>

namespace semantic_fs::ocr {
namespace {

struct OcrAttemptProfile {
    std::vector<std::string> preferredRegionLabels;
    std::vector<std::string> variantNames;
    std::vector<int> pageSegmentationModes;
    double earlyStopConfidence = 1.0;
    double readableTextConfidence = 1.0;
    std::size_t readableTextCharacters = 0;
    int maxAttempts = 0;
};

OcrAttemptProfile profileFor(OcrSearchMode mode)
{
    if (mode == OcrSearchMode::Exhaustive) {
        return {
            .preferredRegionLabels = {
                "social_caption_band",
                "upper_caption_area",
                "center_text_area",
                "lower_caption_area",
                "full_image"
            },
            .variantNames = {
                "scaled_3x",
                "grayscale_scaled_3x",
                "binary_threshold_190",
                "binary_threshold_190_inverted",
                "binary_threshold_140",
                "binary_threshold_140_inverted",
                "sharpened_grayscale"
            },
            .pageSegmentationModes = { 6, 7, 11, 13 },
            .earlyStopConfidence = 0.98,
            .readableTextConfidence = 0.70,
            .readableTextCharacters = 200,
            .maxAttempts = 112
        };
    }

    if (mode == OcrSearchMode::Balanced) {
        return {
            .preferredRegionLabels = {
                "social_caption_band",
                "upper_caption_area",
                "lower_caption_area",
                "full_image"
            },
            .variantNames = {
                "binary_threshold_190",
                "binary_threshold_140",
                "scaled_3x",
                "sharpened_grayscale"
            },
            .pageSegmentationModes = { 6, 7, 11 },
            .earlyStopConfidence = 0.84,
            .readableTextConfidence = 0.45,
            .readableTextCharacters = 120,
            .maxAttempts = 36
        };
    }

    return {
        .preferredRegionLabels = {
            "social_caption_band",
            "upper_caption_area",
            "lower_caption_area"
        },
        .variantNames = {
            "binary_threshold_190",
            "grayscale_scaled_3x"
        },
        .pageSegmentationModes = { 6 },
        .earlyStopConfidence = 0.82,
        .readableTextConfidence = 0.20,
        .readableTextCharacters = 80,
        .maxAttempts = 6
    };
}

std::vector<TextRegion> orderRegions(
    const std::vector<TextRegion>& regions,
    const std::vector<std::string>& preferredLabels
)
{
    std::vector<TextRegion> ordered;
    for (const auto& label : preferredLabels) {
        const auto found = std::find_if(regions.begin(), regions.end(), [&label](const TextRegion& region) {
            return region.label == label;
        });
        if (found != regions.end()) {
            ordered.push_back(*found);
        }
    }

    for (const auto& region : regions) {
        const auto alreadyAdded = std::find_if(ordered.begin(), ordered.end(), [&region](const TextRegion& added) {
            return added.label == region.label;
        });
        if (alreadyAdded == ordered.end()) {
            ordered.push_back(region);
        }
    }

    return ordered;
}

bool isMostlyContained(const std::string& existing, const std::string& candidate)
{
    if (existing.empty() || candidate.empty()) {
        return false;
    }

    if (existing.find(candidate) != std::string::npos || candidate.find(existing) != std::string::npos) {
        return true;
    }

    return false;
}

} // namespace

OcrAttemptRunner::OcrAttemptRunner(OcrSearchMode mode)
    : ocrExtractor_("eng", "tessdata"),
      mode_(mode)
{
}

OcrAttemptResult OcrAttemptRunner::runBestAttempt(const std::filesystem::path& imagePath) const
{
    OcrAttemptResult best;
    const auto profile = profileFor(mode_);
    const auto regions = orderRegions(
        regionDetector_.detect(imagePath),
        profile.preferredRegionLabels
    );

    int attempts = 0;
    for (const auto& region : regions) {
        const auto variants = preprocessor_.preprocessSelectedVariants(
            imagePath,
            region,
            profile.variantNames
        );

        for (const auto& variant : variants) {
            for (const auto pageSegmentationMode : profile.pageSegmentationModes) {
                std::string text;
                try {
                    text = ocrExtractor_.extractText(
                        variant.path,
                        pageSegmentationMode
                    );
                } catch (const std::exception&) {
                    ++attempts;
                    continue;
                }

                const auto score = scoreText(text, region, variant.description);
                ++attempts;

                if (score > best.confidence) {
                    best = {
                        .text = text,
                        .imagePath = variant.path,
                        .regionLabel = region.label,
                        .variantName = variant.description,
                        .pageSegmentationMode = pageSegmentationMode,
                        .confidence = score
                    };
                }

                if (best.confidence >= profile.earlyStopConfidence
                    || (best.confidence >= profile.readableTextConfidence
                        && best.text.size() >= profile.readableTextCharacters)
                    || attempts >= profile.maxAttempts) {
                    return best;
                }
            }
        }
    }

    return best;
}

std::vector<OcrAttemptResult> OcrAttemptRunner::runReadableAttempts(
    const std::filesystem::path& imagePath
) const
{
    std::vector<OcrAttemptResult> readableResults;
    const auto profile = profileFor(mode_);
    const auto regions = orderRegions(
        regionDetector_.detect(imagePath),
        profile.preferredRegionLabels
    );

    int attempts = 0;
    for (const auto& region : regions) {
        const auto variants = preprocessor_.preprocessSelectedVariants(
            imagePath,
            region,
            profile.variantNames
        );

        OcrAttemptResult bestForRegion;
        for (const auto& variant : variants) {
            for (const auto pageSegmentationMode : profile.pageSegmentationModes) {
                std::string text;
                try {
                    text = ocrExtractor_.extractText(
                        variant.path,
                        pageSegmentationMode
                    );
                } catch (const std::exception&) {
                    ++attempts;
                    continue;
                }

                const auto score = scoreText(text, region, variant.description);
                ++attempts;
                if (score > bestForRegion.confidence) {
                    bestForRegion = {
                        .text = text,
                        .imagePath = variant.path,
                        .regionLabel = region.label,
                        .variantName = variant.description,
                        .pageSegmentationMode = pageSegmentationMode,
                        .confidence = score
                    };
                }

                if (attempts >= profile.maxAttempts) {
                    break;
                }
            }

            if (attempts >= profile.maxAttempts) {
                break;
            }
        }

        if (bestForRegion.confidence >= 0.12 && bestForRegion.text.size() >= 20) {
            const auto duplicate = std::find_if(
                readableResults.begin(),
                readableResults.end(),
                [&bestForRegion](const OcrAttemptResult& existing) {
                    return isMostlyContained(existing.text, bestForRegion.text);
                }
            );

            if (duplicate == readableResults.end()) {
                readableResults.push_back(std::move(bestForRegion));
            }
        }

        if (attempts >= profile.maxAttempts) {
            break;
        }
    }

    if (readableResults.empty()) {
        const auto best = runBestAttempt(imagePath);
        if (!best.text.empty()) {
            readableResults.push_back(best);
        }
    }

    return readableResults;
}

double OcrAttemptRunner::scoreText(
    const std::string& text,
    const TextRegion& region,
    const std::string& variantName
) const
{
    if (text.empty()) {
        return 0.0;
    }

    int letters = 0;
    int spaces = 0;
    int suspicious = 0;
    int words = 0;
    int singleLetterWords = 0;
    int longJoinedWords = 0;

    for (const unsigned char value : text) {
        if (std::isalpha(value) != 0) {
            ++letters;
        } else if (std::isspace(value) != 0) {
            ++spaces;
        } else if (std::isdigit(value) == 0 && value != '.' && value != ',' && value != '\'' && value != '-') {
            ++suspicious;
        }
    }

    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        const auto alphaCount = std::count_if(word.begin(), word.end(), [](unsigned char value) {
            return std::isalpha(value) != 0;
        });

        if (alphaCount > 0) {
            ++words;
        }
        if (alphaCount == 1) {
            ++singleLetterWords;
        }
        if (alphaCount > 18) {
            ++longJoinedWords;
        }
    }

    const auto usefulCharacters = letters + spaces;
    if (usefulCharacters == 0) {
        return 0.0;
    }

    const double cleanliness = static_cast<double>(usefulCharacters)
        / static_cast<double>(text.size());
    const double lengthScore = std::min(1.0, static_cast<double>(letters) / 80.0);
    const double wordScore = std::min(1.0, static_cast<double>(words) / 10.0);
    const double suspiciousPenalty = std::min(0.50, static_cast<double>(suspicious) / 40.0);
    const double singleLetterPenalty = std::min(0.25, static_cast<double>(singleLetterWords) / 20.0);
    const double joinedWordPenalty = std::min(0.20, static_cast<double>(longJoinedWords) / 5.0);
    const double variantBoost = variantName.find("binary") != std::string::npos ? 0.03 : 0.0;

    return std::max(
        0.0,
        (cleanliness * 0.42)
            + (lengthScore * 0.22)
            + (wordScore * 0.21)
            + (region.confidence * 0.10)
            + variantBoost
            - suspiciousPenalty
            - singleLetterPenalty
            - joinedWordPenalty
    );
}

} // namespace semantic_fs::ocr
