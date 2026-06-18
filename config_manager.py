import json
from dataclasses import dataclass
from typing import Dict, Any

@dataclass(frozen=True)
class DatasetConfig:
    path: str
    dimension: int
    models_output_dir: str
    encoding_output_dir: str

@dataclass(frozen=True)
class IVFConfig:
    n_list: int
    max_iter: int
    seed: int
    tolerance: float

@dataclass(frozen=True)
class PQConfig:
    m_subvectors: int
    k_subcentroids: int
    max_iter: int
    seed: int
    tolerance: float

class IVFPQConfigManager:
    
    def __init__(self, config_path: str):
        self.config_path = config_path
        self.raw_config = self.load_json()
        
        self.dataset = DatasetConfig(**self.raw_config["dataset"])
        self.ivf = IVFConfig(**self.raw_config["ivf"])
        self.pq = PQConfig(**self.raw_config["pq"])
        
        self.validate_configs()


    def load_json(self) -> Dict[str, Any]:
        with open(self.config_path, "r") as f:
            return json.load(f)


    def validate_configs(self) -> None:
        if (self.dataset.dimension % self.pq.m_subvectors) != 0:
            raise ValueError(
                f"Dataset dimension ({self.dataset.dimension}) must be perfectly "
                f"divisible by the number of PQ subvectors ({self.pq.m_subvectors})."
            )
        print("✓ Configuration successfully loaded and validated.")